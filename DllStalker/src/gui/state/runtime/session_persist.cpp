#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/runtime/session_persist.h"

#include "dumper/instances/collection_view.h"
#include "gui/session_state.h"
#include "gui/state/navigation/history_steady_time.h"
#include "gui/views/sidebar/class_label_lookup.h"
#include "services/module_path.h"
#include "unity_resolver.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace Gui::State
{
namespace
{
constexpr int kSchemaVersion = 1;
constexpr size_t kMaxSessionFileBytes = 16u * 1024u * 1024u;
constexpr int kMaxJsonDepth = 32;
constexpr const char* kStaleInstanceStatus =
    "Instance no longer valid — likely collected or memory changed.";

struct BookmarkCrumbRecipe {
    std::string label{};
    std::string field{};
    bool        isCollection = false;
    bool        isValueTypeSlot = false;
    size_t      valueTypeIndex = 0;
    bool        hasValueTypeIndex = false;
};

struct BookmarkRecipe {
    std::string name{};
    std::string image{};
    std::string classNs{};
    std::string className{};
    std::string rootClassNs{};
    std::string rootClassName{};
    std::vector<BookmarkCrumbRecipe> breadcrumbs{};
};

bool IsSynthesizedFieldsElementName(const std::string& name) {
    size_t index = 0;
    return Engine::Dumper::ParseSynthesizedFieldsElementIndex(name, index);
}

// Labels are "[i]" or "[i] TypeName". ParseSynthesizedFieldsElementIndex
// only accepts exact "[i]" (back()==']'). Stop at the first ']'.
bool ParseLabelBracketIndexPrefix(const std::string& label, size_t& outIndex) {
    if (label.size() < 3 || label.front() != '[') {
        return false;
    }
    const size_t close = label.find(']');
    if (close == std::string::npos || close < 2) {
        return false;
    }
    const char* begin = label.data() + 1;
    const char* end = label.data() + close;
    size_t index = 0;
    auto [ptr, ec] = std::from_chars(begin, end, index);
    if (ec != std::errc{} || ptr != end) {
        return false;
    }
    outIndex = index;
    return true;
}

struct WatchRecipe {
    std::string image{};
    std::string classNs{};
    std::string className{};
    std::string field{};
    std::string type{};
    bool        isStatic = false;
    uint32_t    offset = 0;
    bool        plotEnabled = false;
    WatchPlotMode plotMode = WatchPlotMode::EverySample;
};

std::filesystem::path SessionDirectory() {
    return std::filesystem::path(Engine::Services::GetProxyDllDirectory())
        / L"stalker_runtime" / L"session";
}

void EnsureSessionDirectory() {
    std::error_code ec;
    std::filesystem::create_directories(SessionDirectory(), ec);
}

std::string CombineClassName(const std::string& ns, const std::string& name) {
    if (ns.empty()) {
        return name;
    }
    if (name.empty()) {
        return ns;
    }
    return ns + "::" + name;
}

void SplitClassName(const std::string& display, std::string& ns, std::string& name) {
    const size_t pos = display.rfind("::");
    if (pos == std::string::npos) {
        ns.clear();
        name = display;
        return;
    }
    ns = display.substr(0, pos);
    name = display.substr(pos + 2);
}

const char* PlotModeString(WatchPlotMode mode) {
    return mode == WatchPlotMode::OnChange ? "OnChange" : "EverySample";
}

WatchPlotMode PlotModeFromString(const std::string& s) {
    return s == "OnChange" ? WatchPlotMode::OnChange : WatchPlotMode::EverySample;
}

void AppendJsonString(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
}

void AppendIndent(std::string& out, int indent) {
    out.append(static_cast<size_t>(indent) * 2, ' ');
}

void AppendFieldSep(std::string& out, bool& first) {
    if (!first) {
        out += ",\n";
    }
    first = false;
}

void AppendStringField(std::string& out, int indent, bool& first, const char* key,
                       const std::string& value) {
    AppendFieldSep(out, first);
    AppendIndent(out, indent);
    out += '"';
    out += key;
    out += "\": ";
    AppendJsonString(out, value);
}

void AppendRawField(std::string& out, int indent, bool& first, const char* key,
                    const std::string& raw) {
    AppendFieldSep(out, first);
    AppendIndent(out, indent);
    out += '"';
    out += key;
    out += "\": ";
    out += raw;
}

bool WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    EnsureSessionDirectory();
    const std::filesystem::path tmp{path.native() + L".tmp"};
    {
        std::ofstream out(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!out) {
            out.close();
            DeleteFileW(tmp.c_str());
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            DeleteFileW(tmp.c_str());
            return false;
        }
    }
    if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) == FALSE) {
        return false;
    }
    return true;
}

bool SessionFileExists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

void QuarantineSessionFile(const std::filesystem::path& path) {
    const std::filesystem::path bad{path.native() + L".bad"};
    MoveFileExW(path.c_str(), bad.c_str(), 0);
}

bool ReadTextFile(const std::filesystem::path& path, std::string& text) {
    text.clear();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return false;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxSessionFileBytes) {
        return false;
    }
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) {
        return false;
    }
    text.assign(static_cast<size_t>(size), '\0');
    if (size > 0) {
        in.read(text.data(), static_cast<std::streamsize>(size));
        if (in.gcount() != static_cast<std::streamsize>(size)) {
            text.clear();
            return false;
        }
    }
    return true;
}

struct JsonValue {
    enum class Kind { Null, Bool, Int, String, Array, Object };
    Kind kind = Kind::Null;
    bool b = false;
    int64_t i = 0;
    std::string s{};
    std::vector<JsonValue> arr{};
    std::vector<std::pair<std::string, JsonValue>> obj{};

    const JsonValue* Find(const char* key) const {
        if (kind != Kind::Object || !key) {
            return nullptr;
        }
        for (const auto& kv : obj) {
            if (kv.first == key) {
                return &kv.second;
            }
        }
        return nullptr;
    }

    bool AsBool(bool fallback = false) const {
        return kind == Kind::Bool ? b : fallback;
    }

    bool AsInt64(int64_t& out) const {
        if (kind != Kind::Int) {
            return false;
        }
        out = i;
        return true;
    }

    const std::string* AsString() const {
        return kind == Kind::String ? &s : nullptr;
    }
};

struct JsonCursor {
    const char* p = nullptr;
    const char* end = nullptr;
    int depth = 0;
};

void SkipWs(JsonCursor& c) {
    while (c.p < c.end) {
        const unsigned char ch = static_cast<unsigned char>(*c.p);
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
            break;
        }
        ++c.p;
    }
}

bool ParseValue(JsonCursor& c, JsonValue& out);

int HexNibble(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

void AppendUtf8(std::string& out, unsigned code) {
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    }
    else if (code < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
    else {
        out.push_back(static_cast<char>(0xE0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

bool ParseString(JsonCursor& c, std::string& out) {
    out.clear();
    if (c.p >= c.end || *c.p != '"') {
        return false;
    }
    ++c.p;
    while (c.p < c.end) {
        const char ch = *c.p++;
        if (ch == '"') {
            return true;
        }
        if (static_cast<unsigned char>(ch) < 0x20) {
            return false;
        }
        if (ch != '\\') {
            out.push_back(ch);
            continue;
        }
        if (c.p >= c.end) {
            return false;
        }
        const char esc = *c.p++;
        switch (esc) {
        case '"':
        case '\\':
        case '/':
            out.push_back(esc);
            break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
            unsigned code = 0;
            for (int i = 0; i < 4; ++i) {
                if (c.p >= c.end) {
                    return false;
                }
                const int nib = HexNibble(*c.p++);
                if (nib < 0) {
                    return false;
                }
                code = (code << 4) | static_cast<unsigned>(nib);
            }
            AppendUtf8(out, code);
            break;
        }
        default:
            return false;
        }
    }
    return false;
}

bool ConsumeLiteral(JsonCursor& c, const char* lit) {
    const size_t n = std::strlen(lit);
    if (static_cast<size_t>(c.end - c.p) < n) {
        return false;
    }
    if (std::strncmp(c.p, lit, n) != 0) {
        return false;
    }
    c.p += n;
    return true;
}

bool ParseNumber(JsonCursor& c, JsonValue& out) {
    const char* start = c.p;
    if (c.p < c.end && *c.p == '-') {
        ++c.p;
    }
    if (c.p >= c.end || !std::isdigit(static_cast<unsigned char>(*c.p))) {
        return false;
    }
    if (*c.p == '0') {
        ++c.p;
    }
    else {
        while (c.p < c.end && std::isdigit(static_cast<unsigned char>(*c.p))) {
            ++c.p;
        }
    }
    bool isInt = true;
    if (c.p < c.end && *c.p == '.') {
        isInt = false;
        ++c.p;
        if (c.p >= c.end || !std::isdigit(static_cast<unsigned char>(*c.p))) {
            return false;
        }
        while (c.p < c.end && std::isdigit(static_cast<unsigned char>(*c.p))) {
            ++c.p;
        }
    }
    if (c.p < c.end && (*c.p == 'e' || *c.p == 'E')) {
        isInt = false;
        ++c.p;
        if (c.p < c.end && (*c.p == '+' || *c.p == '-')) {
            ++c.p;
        }
        if (c.p >= c.end || !std::isdigit(static_cast<unsigned char>(*c.p))) {
            return false;
        }
        while (c.p < c.end && std::isdigit(static_cast<unsigned char>(*c.p))) {
            ++c.p;
        }
    }
    if (!isInt) {
        out.kind = JsonValue::Kind::Null;
        return true;
    }
    try {
        out.kind = JsonValue::Kind::Int;
        out.i = std::stoll(std::string(start, c.p));
    }
    catch (...) {
        return false;
    }
    return true;
}

bool ParseArray(JsonCursor& c, JsonValue& out) {
    out.kind = JsonValue::Kind::Array;
    if (c.p >= c.end || *c.p != '[') {
        return false;
    }
    ++c.p;
    ++c.depth;
    if (c.depth > kMaxJsonDepth) {
        return false;
    }
    SkipWs(c);
    if (c.p < c.end && *c.p == ']') {
        ++c.p;
        --c.depth;
        return true;
    }
    for (;;) {
        JsonValue item;
        if (!ParseValue(c, item)) {
            return false;
        }
        out.arr.push_back(std::move(item));
        SkipWs(c);
        if (c.p < c.end && *c.p == ']') {
            ++c.p;
            --c.depth;
            return true;
        }
        if (c.p >= c.end || *c.p != ',') {
            return false;
        }
        ++c.p;
        SkipWs(c);
    }
}

bool ParseObject(JsonCursor& c, JsonValue& out) {
    out.kind = JsonValue::Kind::Object;
    if (c.p >= c.end || *c.p != '{') {
        return false;
    }
    ++c.p;
    ++c.depth;
    if (c.depth > kMaxJsonDepth) {
        return false;
    }
    SkipWs(c);
    if (c.p < c.end && *c.p == '}') {
        ++c.p;
        --c.depth;
        return true;
    }
    for (;;) {
        SkipWs(c);
        std::string key;
        if (!ParseString(c, key)) {
            return false;
        }
        SkipWs(c);
        if (c.p >= c.end || *c.p != ':') {
            return false;
        }
        ++c.p;
        JsonValue value;
        if (!ParseValue(c, value)) {
            return false;
        }
        out.obj.emplace_back(std::move(key), std::move(value));
        SkipWs(c);
        if (c.p < c.end && *c.p == '}') {
            ++c.p;
            --c.depth;
            return true;
        }
        if (c.p >= c.end || *c.p != ',') {
            return false;
        }
        ++c.p;
    }
}

bool ParseValue(JsonCursor& c, JsonValue& out) {
    SkipWs(c);
    if (c.p >= c.end) {
        return false;
    }
    const char ch = *c.p;
    if (ch == '"') {
        out.kind = JsonValue::Kind::String;
        return ParseString(c, out.s);
    }
    if (ch == '{') {
        return ParseObject(c, out);
    }
    if (ch == '[') {
        return ParseArray(c, out);
    }
    if (ch == 't') {
        if (!ConsumeLiteral(c, "true")) {
            return false;
        }
        out.kind = JsonValue::Kind::Bool;
        out.b = true;
        return true;
    }
    if (ch == 'f') {
        if (!ConsumeLiteral(c, "false")) {
            return false;
        }
        out.kind = JsonValue::Kind::Bool;
        out.b = false;
        return true;
    }
    if (ch == 'n') {
        if (!ConsumeLiteral(c, "null")) {
            return false;
        }
        out.kind = JsonValue::Kind::Null;
        return true;
    }
    if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
        return ParseNumber(c, out);
    }
    return false;
}

bool ParseDocument(const std::string& text, JsonValue& root) {
    JsonCursor c{};
    c.p = text.data();
    c.end = text.data() + text.size();
    if (text.size() >= 3
        && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF) {
        c.p += 3;
    }
    if (!ParseValue(c, root)) {
        return false;
    }
    SkipWs(c);
    return c.p == c.end;
}

bool SchemaVersionIsOne(const JsonValue& root) {
    const JsonValue* ver = root.Find("schema_version");
    int64_t n = 0;
    return ver && ver->AsInt64(n) && n == kSchemaVersion;
}

std::string JsonStringField(const JsonValue& obj, const char* key) {
    const JsonValue* v = obj.Find(key);
    const std::string* s = v ? v->AsString() : nullptr;
    return s ? *s : std::string{};
}

bool LoadBookmarkRecipes(const JsonValue& root, std::vector<BookmarkRecipe>& out) {
    out.clear();
    if (!SchemaVersionIsOne(root)) {
        return false;
    }
    const JsonValue* arr = root.Find("bookmarks");
    if (!arr || arr->kind != JsonValue::Kind::Array) {
        return false;
    }
    for (const auto& item : arr->arr) {
        if (item.kind != JsonValue::Kind::Object) {
            continue;
        }
        BookmarkRecipe recipe{};
        recipe.name = JsonStringField(item, "name");
        recipe.image = JsonStringField(item, "image");
        recipe.classNs = JsonStringField(item, "class_ns");
        recipe.className = JsonStringField(item, "class_name");
        recipe.rootClassNs = JsonStringField(item, "root_class_ns");
        recipe.rootClassName = JsonStringField(item, "root_class_name");
        if (recipe.name.empty()) {
            continue;
        }
        const JsonValue* crumbs = item.Find("breadcrumbs");
        if (crumbs && crumbs->kind == JsonValue::Kind::Array) {
            for (const auto& crumbVal : crumbs->arr) {
                if (crumbVal.kind != JsonValue::Kind::Object) {
                    continue;
                }
                BookmarkCrumbRecipe crumb{};
                crumb.label = JsonStringField(crumbVal, "label");
                crumb.field = JsonStringField(crumbVal, "field");
                const JsonValue* coll = crumbVal.Find("isCollection");
                crumb.isCollection = coll ? coll->AsBool(false) : false;
                const JsonValue* slot = crumbVal.Find("isValueTypeSlot");
                crumb.isValueTypeSlot = slot ? slot->AsBool(false) : false;
                int64_t index = 0;
                const JsonValue* indexVal = crumbVal.Find("valueTypeIndex");
                if (indexVal && indexVal->AsInt64(index) && index >= 0) {
                    crumb.valueTypeIndex = static_cast<size_t>(index);
                    crumb.hasValueTypeIndex = true;
                }
                recipe.breadcrumbs.push_back(std::move(crumb));
            }
        }
        out.push_back(std::move(recipe));
    }
    return true;
}

bool LoadWatchRecipes(const JsonValue& root, std::vector<WatchRecipe>& out) {
    out.clear();
    if (!SchemaVersionIsOne(root)) {
        return false;
    }
    const JsonValue* arr = root.Find("watches");
    if (!arr || arr->kind != JsonValue::Kind::Array) {
        return false;
    }
    for (const auto& item : arr->arr) {
        if (out.size() >= FieldWatchModel::kMaxEntries) {
            break;
        }
        if (item.kind != JsonValue::Kind::Object) {
            continue;
        }
        WatchRecipe recipe{};
        recipe.image = JsonStringField(item, "image");
        recipe.classNs = JsonStringField(item, "class_ns");
        recipe.className = JsonStringField(item, "class_name");
        recipe.field = JsonStringField(item, "field");
        recipe.type = JsonStringField(item, "type");
        const JsonValue* isStatic = item.Find("isStatic");
        recipe.isStatic = isStatic ? isStatic->AsBool(false) : false;
        int64_t offset = 0;
        const JsonValue* offsetVal = item.Find("offset");
        if (offsetVal && offsetVal->AsInt64(offset) && offset > 0) {
            recipe.offset = offset > 0xFFFFFFFFLL
                ? 0xFFFFFFFFu : static_cast<uint32_t>(offset);
        }
        const JsonValue* plotEnabled = item.Find("plotEnabled");
        recipe.plotEnabled = plotEnabled ? plotEnabled->AsBool(false) : false;
        recipe.plotMode = PlotModeFromString(JsonStringField(item, "plotMode"));
        if (recipe.field.empty()) {
            continue;
        }
        out.push_back(std::move(recipe));
    }
    return true;
}

Bookmark BookmarkFromRecipe(const BookmarkRecipe& recipe, uint32_t id) {
    Bookmark entry{};
    entry.id = id;
    entry.name = recipe.name;
    entry.createdAtSec = 0.0;
    entry.snapshot.imageName = recipe.image;
    entry.snapshot.className = CombineClassName(recipe.classNs, recipe.className);
    entry.snapshot.rootClassName = CombineClassName(recipe.rootClassNs, recipe.rootClassName);
    entry.snapshot.imagePtr = nullptr;
    entry.snapshot.classPtr = nullptr;
    entry.snapshot.instancePtr = nullptr;
    entry.snapshot.instanceIndex = -1;
    entry.snapshot.breadcrumbs.reserve(recipe.breadcrumbs.size());
    for (size_t i = 0; i < recipe.breadcrumbs.size(); ++i) {
        const auto& crumbRecipe = recipe.breadcrumbs[i];
        InspectorBreadcrumb crumb{};
        crumb.label = crumbRecipe.label;
        crumb.isCollection = crumbRecipe.isCollection;
        crumb.isValueTypeSlot = crumbRecipe.isValueTypeSlot;
        crumb.klass = nullptr;
        crumb.instance = nullptr;
        crumb.sourceField.valueAddress = 0;
        crumb.sourceField.hasValue = false;
        if (i > 0
            && !crumbRecipe.field.empty()
            && !IsSynthesizedFieldsElementName(crumbRecipe.field)) {
            crumb.sourceField.name = crumbRecipe.field;
        }
        if (crumbRecipe.hasValueTypeIndex) {
            crumb.valueTypeIndex = crumbRecipe.valueTypeIndex;
            crumb.hasValueTypeIndex = true;
        }
        entry.snapshot.breadcrumbs.push_back(std::move(crumb));
    }
    entry.snapshot.summaryLabel = NavigationLocationLabel(entry.snapshot);
    return entry;
}

WatchedField WatchFromRecipe(const WatchRecipe& recipe, uint32_t id) {
    WatchedField entry{};
    entry.id = id;
    entry.baseAddress = 0;
    entry.offset = recipe.offset;
    entry.readAddress = 0;
    entry.className = CombineClassName(recipe.classNs, recipe.className);
    entry.fieldName = recipe.field;
    entry.typeName = recipe.type;
    entry.isStatic = recipe.isStatic;
    entry.restoreSnapshot.imageName = recipe.image;
    entry.restoreSnapshot.className = entry.className;
    entry.restoreSnapshot.imagePtr = nullptr;
    entry.restoreSnapshot.classPtr = nullptr;
    entry.restoreSnapshot.instancePtr = nullptr;
    entry.restoreSnapshot.instanceIndex = -1;
    entry.stale = true;
    entry.plotEnabled = recipe.plotEnabled;
    entry.plotMode = recipe.plotMode;
    entry.hasCustomPlotMode = false;
    return entry;
}

std::string BuildBookmarksJson(const InspectorBookmarksModel& bookmarks) {
    std::string out;
    out += "{\n";
    AppendIndent(out, 1);
    out += "\"schema_version\": 1,\n";
    AppendIndent(out, 1);
    out += "\"bookmarks\": [\n";
    for (size_t i = 0; i < bookmarks.bookmarks.size(); ++i) {
        const Bookmark& bm = bookmarks.bookmarks[i];
        std::string classNs;
        std::string className;
        SplitClassName(bm.snapshot.className, classNs, className);

        AppendIndent(out, 2);
        out += "{\n";
        bool first = true;
        AppendStringField(out, 3, first, "name", bm.name);
        AppendStringField(out, 3, first, "image", bm.snapshot.imageName);
        if (!classNs.empty()) {
            AppendStringField(out, 3, first, "class_ns", classNs);
        }
        AppendStringField(out, 3, first, "class_name", className);
        std::string rootNs;
        std::string rootName;
        SplitClassName(bm.snapshot.rootClassName, rootNs, rootName);
        if (!rootNs.empty()) {
            AppendStringField(out, 3, first, "root_class_ns", rootNs);
        }
        if (!rootName.empty()) {
            AppendStringField(out, 3, first, "root_class_name", rootName);
        }
        if (!bm.snapshot.breadcrumbs.empty()) {
            AppendFieldSep(out, first);
            AppendIndent(out, 3);
            out += "\"breadcrumbs\": [\n";
            for (size_t c = 0; c < bm.snapshot.breadcrumbs.size(); ++c) {
                const auto& crumb = bm.snapshot.breadcrumbs[c];
                AppendIndent(out, 4);
                out += "{\n";
                bool crumbFirst = true;
                AppendStringField(out, 5, crumbFirst, "label", crumb.label);
                if (c > 0
                    && !crumb.sourceField.name.empty()
                    && !IsSynthesizedFieldsElementName(crumb.sourceField.name)) {
                    AppendStringField(out, 5, crumbFirst, "field", crumb.sourceField.name);
                }
                if (crumb.isCollection) {
                    AppendRawField(out, 5, crumbFirst, "isCollection", "true");
                }
                if (crumb.isValueTypeSlot) {
                    AppendRawField(out, 5, crumbFirst, "isValueTypeSlot", "true");
                }
                if (crumb.hasValueTypeIndex) {
                    AppendRawField(out, 5, crumbFirst, "valueTypeIndex",
                                   std::to_string(crumb.valueTypeIndex));
                }
                out += "\n";
                AppendIndent(out, 4);
                out += "}";
                if (c + 1 < bm.snapshot.breadcrumbs.size()) {
                    out += ",";
                }
                out += "\n";
            }
            AppendIndent(out, 3);
            out += "]";
        }
        out += "\n";
        AppendIndent(out, 2);
        out += "}";
        if (i + 1 < bookmarks.bookmarks.size()) {
            out += ",";
        }
        out += "\n";
    }
    AppendIndent(out, 1);
    out += "]\n";
    out += "}\n";
    return out;
}

std::string BuildWatchesJson(const std::vector<WatchedField>& entries) {
    std::string out;
    out += "{\n";
    AppendIndent(out, 1);
    out += "\"schema_version\": 1,\n";
    AppendIndent(out, 1);
    out += "\"watches\": [\n";
    const size_t count = (std::min)(entries.size(), FieldWatchModel::kMaxEntries);
    for (size_t i = 0; i < count; ++i) {
        const WatchedField& w = entries[i];
        std::string classNs;
        std::string className;
        SplitClassName(w.restoreSnapshot.className.empty() ? w.className : w.restoreSnapshot.className,
                       classNs, className);
        const std::string image = w.restoreSnapshot.imageName;

        AppendIndent(out, 2);
        out += "{\n";
        bool first = true;
        AppendStringField(out, 3, first, "image", image);
        if (!classNs.empty()) {
            AppendStringField(out, 3, first, "class_ns", classNs);
        }
        AppendStringField(out, 3, first, "class_name", className);
        AppendStringField(out, 3, first, "field", w.fieldName);
        AppendStringField(out, 3, first, "type", w.typeName);
        if (w.isStatic) {
            AppendRawField(out, 3, first, "isStatic", "true");
        }
        AppendRawField(out, 3, first, "offset", std::to_string(w.offset));
        if (w.plotEnabled) {
            AppendRawField(out, 3, first, "plotEnabled", "true");
        }
        if (w.plotMode == WatchPlotMode::OnChange) {
            AppendStringField(out, 3, first, "plotMode", PlotModeString(w.plotMode));
        }
        out += "\n";
        AppendIndent(out, 2);
        out += "}";
        if (i + 1 < count) {
            out += ",";
        }
        out += "\n";
    }
    AppendIndent(out, 1);
    out += "]\n";
    out += "}\n";
    return out;
}

void* ResolveImagePtr(const Gui::ControlPanelSessionState& state, const std::string& imageName) {
    if (imageName.empty() || imageName == "<image>") {
        return nullptr;
    }
    for (const auto& img : *state.GetImageCacheSnapshot()) {
        if (img.name == imageName) {
            return img.imagePtr;
        }
    }
    return nullptr;
}

void* ResolveClassPtr(const Gui::ControlPanelSessionState& state,
                      void* imagePtr,
                      const std::string& ns,
                      const std::string& name) {
    if (!imagePtr || name.empty() || name == "<class>") {
        return nullptr;
    }

    const auto& exp = Engine::Unity.module.exports;
    if (exp.fnGetClass) {
        if (void* klass = exp.fnGetClass(imagePtr, ns.c_str(), name.c_str())) {
            return klass;
        }
    }

    if (state.selectedImage != imagePtr) {
        return nullptr;
    }
    const std::string display = CombineClassName(ns, name);
    for (const auto& cl : *state.GetClassCacheSnapshot()) {
        if (cl.ns == ns && cl.name == name) {
            return cl.klassPtr;
        }
        const std::string clDisplay = CombineClassName(cl.ns, cl.name);
        if (clDisplay == display) {
            return cl.klassPtr;
        }
    }
    return nullptr;
}

bool TryRebindStaticWatch(Gui::ControlPanelSessionState& state,
                          void* klass,
                          const std::string& fieldName,
                          uintptr_t& outBase,
                          uintptr_t& outRead) {
    outBase = 0;
    outRead = 0;
    if (!klass || !state.dumper || fieldName.empty()) {
        return false;
    }
    const auto fields = state.dumper->GetRawFields(klass, nullptr);
    for (const auto& field : fields) {
        if (field.name != fieldName) {
            continue;
        }
        if (!field.hasValue || field.valueAddress == 0) {
            return false;
        }
        outRead = field.valueAddress;
        outBase = field.valueAddress;
        return true;
    }
    return false;
}

void MarkStaleToast(Gui::ControlPanelSessionState& state) {
    state.navigationFeedback.MarkStatus(kStaleInstanceStatus, HistorySteadyNowSeconds());
}
} // namespace

void SessionPersist::StampBookmarkNamedPath(NavigationSnapshot& snap,
                                            const ControlPanelSessionState* state) {
    if (!snap.breadcrumbs.empty()) {
        const InspectorBreadcrumb& root = snap.breadcrumbs.front();
        if (root.klass && state) {
            std::string display = Gui::Views::LookupClassDisplayName(*state, root.klass);
            if (!display.empty()) {
                snap.rootClassName = std::move(display);
            }
        }
    }

    for (size_t i = 0; i < snap.breadcrumbs.size(); ++i) {
        InspectorBreadcrumb& crumb = snap.breadcrumbs[i];
        if (IsSynthesizedFieldsElementName(crumb.sourceField.name)) {
            crumb.sourceField.name.clear();
            crumb.sourceField.valueAddress = 0;
            crumb.sourceField.hasValue = false;
        }

        if (i == 0) {
            continue;
        }

        const bool parentIsCollection = snap.breadcrumbs[i - 1].isCollection;
        if (crumb.isValueTypeSlot) {
            if (!crumb.hasValueTypeIndex) {
                size_t index = 0;
                if (ParseLabelBracketIndexPrefix(crumb.label, index)) {
                    crumb.valueTypeIndex = index;
                    crumb.hasValueTypeIndex = true;
                }
            }
            continue;
        }

        if (parentIsCollection && !crumb.isCollection) {
            size_t index = 0;
            if (Engine::Dumper::ParseSynthesizedFieldsElementIndex(crumb.label, index)) {
                crumb.valueTypeIndex = index;
                crumb.hasValueTypeIndex = true;
            }
            continue;
        }

        if (crumb.sourceField.name.empty()
            && !crumb.label.empty()
            && !IsSynthesizedFieldsElementName(crumb.label)
            && !crumb.isCollection) {
            crumb.sourceField.name = crumb.label;
            crumb.sourceField.valueAddress = 0;
            crumb.sourceField.hasValue = false;
        }
    }
}

bool SessionPersist::SaveBookmarks(const InspectorBookmarksModel& bookmarks) {
    InspectorBookmarksModel stamped = bookmarks;
    for (auto& bm : stamped.bookmarks) {
        StampBookmarkNamedPath(bm.snapshot, nullptr);
    }
    return WriteTextFile(SessionDirectory() / L"bookmarks.json", BuildBookmarksJson(stamped));
}

bool SessionPersist::SaveWatches(const FieldWatchModel& watches) {
    const std::vector<WatchedField> snap = watches.SnapshotEntries();
    return WriteTextFile(SessionDirectory() / L"watches.json", BuildWatchesJson(snap));
}

void SessionPersist::LoadInto(Gui::ControlPanelSessionState& state) {
    EnsureSessionDirectory();

    {
        const std::filesystem::path bmPath = SessionDirectory() / L"bookmarks.json";
        if (!SessionFileExists(bmPath)) {
            state.bookmarks.bookmarks.clear();
            state.bookmarks.nextId = 1;
        }
        else {
            std::string text;
            JsonValue root;
            std::vector<BookmarkRecipe> recipes;
            if (ReadTextFile(bmPath, text) && ParseDocument(text, root)
                && LoadBookmarkRecipes(root, recipes)) {
                state.bookmarks.bookmarks.clear();
                uint32_t id = 1;
                for (const auto& recipe : recipes) {
                    state.bookmarks.bookmarks.push_back(BookmarkFromRecipe(recipe, id++));
                }
                state.bookmarks.nextId = id;
            }
            else {
                state.navigationFeedback.MarkStatus(
                    "Failed to load bookmarks.json — left unchanged.",
                    HistorySteadyNowSeconds());
                QuarantineSessionFile(bmPath);
            }
        }
    }

    std::vector<WatchedField> loadedWatches;
    uint32_t watchNextId = 1;
    bool assignWatches = false;
    {
        const std::filesystem::path watchPath = SessionDirectory() / L"watches.json";
        if (!SessionFileExists(watchPath)) {
            assignWatches = true;
        }
        else {
            std::string text;
            JsonValue root;
            std::vector<WatchRecipe> recipes;
            if (ReadTextFile(watchPath, text) && ParseDocument(text, root)
                && LoadWatchRecipes(root, recipes)) {
                for (const auto& recipe : recipes) {
                    loadedWatches.push_back(WatchFromRecipe(recipe, watchNextId++));
                }
                assignWatches = true;
            }
            else {
                state.navigationFeedback.MarkStatus(
                    "Failed to load watches.json — left unchanged.",
                    HistorySteadyNowSeconds());
                QuarantineSessionFile(watchPath);
            }
        }
    }

    if (assignWatches) {
        std::lock_guard<std::mutex> lock(state.fieldWatch.entriesMutex);
        state.fieldWatch.entries = std::move(loadedWatches);
        state.fieldWatch.nextId = watchNextId;
        state.fieldWatch.activeCount.store(state.fieldWatch.entries.size(), std::memory_order_relaxed);
        state.fieldWatch.selectedPlotWatchId = 0;
        for (const auto& entry : state.fieldWatch.entries) {
            if (entry.plotEnabled) {
                state.fieldWatch.selectedPlotWatchId = entry.id;
                break;
            }
        }
    }
    state.fieldWatch.NotifySampler();
}

void SessionPersist::Rebind(Gui::ControlPanelSessionState& state) {
    if (!state.dumper) {
        return;
    }

    Engine::Unity.module.EnsureThreadAttached();

    bool toast = false;

    for (auto& bm : state.bookmarks.bookmarks) {
        std::string ns;
        std::string name;
        SplitClassName(bm.snapshot.className, ns, name);
        void* imagePtr = ResolveImagePtr(state, bm.snapshot.imageName);
        void* classPtr = ResolveClassPtr(state, imagePtr, ns, name);
        bm.snapshot.imagePtr = imagePtr;
        bm.snapshot.classPtr = classPtr;
        bm.snapshot.instancePtr = nullptr;
        for (auto& crumb : bm.snapshot.breadcrumbs) {
            crumb.klass = nullptr;
            crumb.instance = nullptr;
        }
    }

    struct WatchSnap {
        uint32_t    id = 0;
        std::string imageName{};
        std::string classDisplay{};
        std::string fieldName{};
        bool        isStatic = false;
    };
    std::vector<WatchSnap> snaps;
    {
        std::lock_guard<std::mutex> lock(state.fieldWatch.entriesMutex);
        snaps.reserve(state.fieldWatch.entries.size());
        for (const auto& entry : state.fieldWatch.entries) {
            WatchSnap job{};
            job.id = entry.id;
            job.imageName = entry.restoreSnapshot.imageName;
            job.classDisplay = entry.restoreSnapshot.className.empty() ? entry.className
                                                                       : entry.restoreSnapshot.className;
            job.fieldName = entry.fieldName;
            job.isStatic = entry.isStatic;
            snaps.push_back(std::move(job));
        }
    }

    struct WatchBind {
        uint32_t  id = 0;
        void*     imagePtr = nullptr;
        void*     classPtr = nullptr;
        bool      isStatic = false;
        bool      staticOk = false;
        uintptr_t baseAddress = 0;
        uintptr_t readAddress = 0;
    };
    std::vector<WatchBind> binds;
    binds.reserve(snaps.size());
    for (const auto& snap : snaps) {
        WatchBind job{};
        job.id = snap.id;
        job.isStatic = snap.isStatic;
        std::string ns;
        std::string name;
        SplitClassName(snap.classDisplay, ns, name);
        job.imagePtr = ResolveImagePtr(state, snap.imageName);
        job.classPtr = ResolveClassPtr(state, job.imagePtr, ns, name);
        if (snap.isStatic) {
            job.staticOk = TryRebindStaticWatch(state, job.classPtr, snap.fieldName,
                                                job.baseAddress, job.readAddress);
            if (!job.staticOk) {
                toast = true;
            }
        }
        else {
            toast = true;
        }
        binds.push_back(job);
    }

    {
        std::lock_guard<std::mutex> lock(state.fieldWatch.entriesMutex);
        for (auto& entry : state.fieldWatch.entries) {
            const WatchBind* job = nullptr;
            for (const auto& candidate : binds) {
                if (candidate.id == entry.id) {
                    job = &candidate;
                    break;
                }
            }
            if (!job) {
                continue;
            }
            entry.restoreSnapshot.imagePtr = job->imagePtr;
            entry.restoreSnapshot.classPtr = job->classPtr;
            entry.restoreSnapshot.instancePtr = nullptr;
            if (job->isStatic && job->staticOk) {
                entry.baseAddress = job->baseAddress;
                entry.readAddress = job->readAddress;
                entry.stale = false;
            }
            else {
                entry.baseAddress = 0;
                entry.readAddress = 0;
                entry.stale = true;
            }
        }
    }
    state.fieldWatch.NotifySampler();

    if (toast) {
        MarkStaleToast(state);
    }
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
