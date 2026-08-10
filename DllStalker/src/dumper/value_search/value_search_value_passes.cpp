#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/value_search/value_search_value_passes.h"

#include "dumper/value_search/value_search_match.h"
#include "types/name_fuzzy.h"
#include "types/type_classifier.h"
#include "types/value_decoder.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace Engine::Dumper
{
namespace
{
constexpr float kFloatMatchEpsilon = 1e-4f;

bool ParseBoolNeedle(const std::string& needle, bool& out) {
    const std::string lower = Types::ToLowerAscii(needle);
    if (lower == "true" || lower == "1" || lower == "yes") {
        out = true;
        return true;
    }
    if (lower == "false" || lower == "0" || lower == "no") {
        out = false;
        return true;
    }
    return false;
}

bool ParseInt64(const std::string& text, int64_t& out) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

bool ParseDouble(const std::string& text, double& out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end && end != text.c_str() && *end == '\0';
}

bool FloatNear(double a, double b) {
    const double diff = std::fabs(a - b);
    const double scale = (std::max)(1.0, (std::max)(std::fabs(a), std::fabs(b)));
    return diff <= static_cast<double>(kFloatMatchEpsilon) * scale
        || diff <= static_cast<double>(kFloatMatchEpsilon);
}

bool MatchBoolValue(const FieldInfo& field, const std::string& valueNeedle) {
    bool want = false;
    if (!ParseBoolNeedle(valueNeedle, want)) {
        return false;
    }
    const std::string lower = Types::ToLowerAscii(field.valueDisplay);
    bool have = false;
    if (!ParseBoolNeedle(lower, have)) {
        return false;
    }
    return have == want;
}

bool MatchNumberValue(const FieldInfo& field, const std::string& valueNeedle) {
    const auto cat = Types::GetCategory(field.isEnum && !field.underlyingType.empty()
                                            ? field.underlyingType
                                            : field.type);
    if (cat == Types::TypeCategory::R4 || cat == Types::TypeCategory::R8) {
        double want = 0.0;
        double have = 0.0;
        if (!ParseDouble(valueNeedle, want) || !ParseDouble(field.valueDisplay, have)) {
            return false;
        }
        return FloatNear(want, have);
    }

    int64_t want = 0;
    int64_t have = 0;
    if (!ParseInt64(valueNeedle, want)) {
        return false;
    }
    // valueDisplay may be hex or decimal — try decimal first, then from_chars base 10 only.
    if (ParseInt64(field.valueDisplay, have)) {
        return have == want;
    }
    // Strip common decoration
    std::string cleaned;
    cleaned.reserve(field.valueDisplay.size());
    for (char c : field.valueDisplay) {
        if ((c >= '0' && c <= '9') || c == '-' || c == '+') {
            cleaned.push_back(c);
        }
    }
    return ParseInt64(cleaned, have) && have == want;
}

// Component-list parse mirrors value_writer.cpp ParseFloatList / ParseByteList
// (parens, brackets, commas → whitespace; exact component count).
void NormalizeComponentSeparators(std::string& buf) {
    for (char& c : buf) {
        if (c == '(' || c == ')' || c == '[' || c == ']' || c == ',') {
            c = ' ';
        }
    }
}

bool ParseFloatComponents(const std::string& text, size_t expected, std::vector<double>& out) {
    out.clear();
    out.reserve(expected);
    std::string buf = text;
    NormalizeComponentSeparators(buf);
    const char* p = buf.c_str();
    while (*p) {
        while (*p && std::isspace(static_cast<unsigned char>(*p))) {
            ++p;
        }
        if (!*p) {
            break;
        }
        char* end = nullptr;
        const double v = std::strtod(p, &end);
        if (end == p) {
            return false;
        }
        out.push_back(v);
        p = end;
    }
    return out.size() == expected;
}

bool ParseByteComponents(const std::string& text, size_t expected, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(expected);
    std::string buf = text;
    NormalizeComponentSeparators(buf);
    const char* p = buf.c_str();
    while (*p) {
        while (*p && std::isspace(static_cast<unsigned char>(*p))) {
            ++p;
        }
        if (!*p) {
            break;
        }
        char* end = nullptr;
        const unsigned long v = std::strtoul(p, &end, 0);
        if (end == p || v > 255u) {
            return false;
        }
        out.push_back(static_cast<uint8_t>(v));
        p = end;
    }
    return out.size() == expected;
}

size_t InlineStructFloatCount(Types::TypeCategory cat) {
    using Cat = Types::TypeCategory;
    switch (cat) {
    case Cat::VEC2:
        return 2;
    case Cat::VEC3:
        return 3;
    case Cat::VEC4:
    case Cat::QUAT:
    case Cat::COLOR:
    case Cat::RECT:
        return 4;
    default:
        return 0;
    }
}

bool MatchInlineStructValue(const FieldInfo& field, const std::string& valueNeedle) {
    const auto cat = Types::GetCategory(field.type);
    if (cat == Types::TypeCategory::COLOR32) {
        std::vector<uint8_t> want;
        std::vector<uint8_t> have;
        if (!ParseByteComponents(valueNeedle, 4, want)
            || !ParseByteComponents(field.valueDisplay, 4, have)) {
            return false;
        }
        return want == have;
    }
    const size_t count = InlineStructFloatCount(cat);
    if (count == 0) {
        return false;
    }
    std::vector<double> want;
    std::vector<double> have;
    if (!ParseFloatComponents(valueNeedle, count, want)
        || !ParseFloatComponents(field.valueDisplay, count, have)) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!FloatNear(want[i], have[i])) {
            return false;
        }
    }
    return true;
}

bool MatchStringValue(const FieldInfo& field,
                      const std::string& valueNeedle,
                      bool valueMatchStrict) {
    // Strip quotes + case-fold; strict == / fuzzy subsequence.
    const std::string have =
        Types::ToLowerAscii(Decode::StripQuotesForFieldEdit(field.valueDisplay));
    const std::string want =
        Types::ToLowerAscii(Decode::StripQuotesForFieldEdit(valueNeedle));
    if (want.empty()) {
        return have.empty();
    }
    if (valueMatchStrict) {
        return have == want;
    }
    return Types::NameFuzzyMatch(have, want);
}

bool MatchEnumValue(UnityDumper& dumper, const FieldInfo& field, const std::string& valueNeedle) {
    // Prefer numeric / display equality against this field's live value.
    if (MatchNumberValue(field, valueNeedle)) {
        return true;
    }
    if (Types::ToLowerAscii(field.valueDisplay) == Types::ToLowerAscii(valueNeedle)) {
        return true;
    }
    if (!field.enumKlass) {
        return false;
    }

    int64_t haveNum = 0;
    bool haveParsed = ParseInt64(field.valueDisplay, haveNum);
    if (!haveParsed) {
        std::string cleaned;
        cleaned.reserve(field.valueDisplay.size());
        for (char c : field.valueDisplay) {
            if ((c >= '0' && c <= '9') || c == '-' || c == '+') {
                cleaned.push_back(c);
            }
        }
        haveParsed = ParseInt64(cleaned, haveNum);
    }

    try {
        const auto literals = dumper.GetEnumLiterals(field.enumKlass);
        for (const auto& lit : literals) {
            if (Types::ToLowerAscii(lit.name) != Types::ToLowerAscii(valueNeedle)) {
                continue;
            }
            // Name needle matches this literal — only accept if the field
            // currently holds that literal's value.
            if (haveParsed && lit.value == haveNum) {
                return true;
            }
            if (Types::ToLowerAscii(field.valueDisplay) == Types::ToLowerAscii(lit.name)) {
                return true;
            }
        }
    }
    catch (...) {
    }
    return false;
}

// Optional null / non-null only — no arbitrary address equality.
enum class PtrNeedleKind { None, Null, NonNull };

PtrNeedleKind ParsePtrNeedle(const std::string& needle) {
    const std::string lower = Types::ToLowerAscii(needle);
    if (lower == "null" || lower == "nullptr" || lower == "nil") {
        return PtrNeedleKind::Null;
    }
    if (lower == "non-null" || lower == "nonnull" || lower == "!null") {
        return PtrNeedleKind::NonNull;
    }
    return PtrNeedleKind::None;
}

bool IsPtrDisplayNull(const std::string& display) {
    // CollectionView remaps decode "null" → "[null]" for Fields rows.
    const std::string lower = Types::ToLowerAscii(display);
    return lower == "null" || lower == "[null]";
}

bool IsPtrDisplayReadableNonNull(const std::string& display) {
    // Decoder honesty: unreadable → "??"; null pointer → "null"; else "0x…".
    if (display.empty() || display == "-" || display == "??") {
        return false;
    }
    if (IsPtrDisplayNull(display)) {
        return false;
    }
    return true;
}

bool MatchPtrValue(const FieldInfo& field, const std::string& valueNeedle) {
    switch (ParsePtrNeedle(valueNeedle)) {
    case PtrNeedleKind::Null:
        return IsPtrDisplayNull(field.valueDisplay);
    case PtrNeedleKind::NonNull:
        return IsPtrDisplayReadableNonNull(field.valueDisplay);
    case PtrNeedleKind::None:
    default:
        return false;
    }
}
} // namespace

bool ValuePasses(UnityDumper& dumper, const FieldInfo& field, const ValueSearchParams& params) {
    if (params.valueNeedle.empty()) {
        // Name-only hits when value box is empty (kind + name gates already ran).
        // Drill never reaches here with empty value (scanner early-out).
        return !params.nameNeedle.empty() || params.drillMode;
    }
    if (field.isEnum) {
        return MatchEnumValue(dumper, field, params.valueNeedle);
    }
    const auto cat = Types::GetCategory(field.type);
    if (cat == Types::TypeCategory::BOOLEAN) {
        return MatchBoolValue(field, params.valueNeedle);
    }
    if (cat == Types::TypeCategory::STRING) {
        return MatchStringValue(field, params.valueNeedle, params.valueMatchStrict);
    }
    if (IsValueSearchNumberCategory(cat)) {
        if (Types::IsInlineValueStruct(cat)) {
            return MatchInlineStructValue(field, params.valueNeedle);
        }
        return MatchNumberValue(field, params.valueNeedle);
    }
    if (cat == Types::TypeCategory::PTR) {
        return MatchPtrValue(field, params.valueNeedle);
    }
    return false;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
