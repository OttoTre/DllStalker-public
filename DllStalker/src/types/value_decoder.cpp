#include "pch.h"

#ifdef ENABLE_DUMPER

#include "types/value_decoder.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

#include "types/memory_guard.h"
#include "types/type_classifier.h"
#include "types/unity_array_layout.h"

namespace Engine::Decode
{
namespace
{
constexpr int32_t UNITY_STRING_LAYOUT_OFFSET = 0x10; // Offset to the length field
constexpr int32_t UNITY_STRING_BUFFER_OFFSET = 0x14; // Offset to the UTF-16 buffer
constexpr int32_t kMaxManagedStringPreviewChars = 1024;
constexpr std::string_view kTruncatedSuffix = "...(truncated)";
} // namespace

namespace
{
std::string Utf16ObjectToQuotedUtf8(uintptr_t managedStringPtr) {
    if (managedStringPtr == 0) {
        return "null";
    }

    int32_t length = 0;
    if (!Memory::TryReadValue(managedStringPtr + UNITY_STRING_LAYOUT_OFFSET, length)) {
        return "<?>";
    }

    if (length < 0) {
        return "\"\"";
    }
    if (length == 0) {
        return "\"\"";
    }

    bool truncated = false;
    if (length > kMaxManagedStringPreviewChars) {
        length = kMaxManagedStringPreviewChars;
        truncated = true;
    }

    std::vector<wchar_t> utf16Buffer(static_cast<size_t>(length));
    const size_t byteSize = static_cast<size_t>(length) * sizeof(wchar_t);

    if (!Memory::TryReadBytes(managedStringPtr + UNITY_STRING_BUFFER_OFFSET,
                              utf16Buffer.data(),
                              byteSize)) {
        return "\"<unreadable>\"";
    }

    std::string utf8Result;
    utf8Result.reserve(static_cast<size_t>(length) + kTruncatedSuffix.size() + 2);

    auto appendUtf8 = [&](char32_t cp) {
        if (cp < 0x80) {
            utf8Result += static_cast<char>(cp);
        }
        else if (cp < 0x800) {
            utf8Result += static_cast<char>(0xC0 | (cp >> 6));
            utf8Result += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000) {
            utf8Result += static_cast<char>(0xE0 | (cp >> 12));
            utf8Result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8Result += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else {
            utf8Result += static_cast<char>(0xF0 | (cp >> 18));
            utf8Result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            utf8Result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8Result += static_cast<char>(0x80 | (cp & 0x3F));
        }
    };

    for (size_t i = 0; i < utf16Buffer.size(); ++i) {
        char32_t cp = static_cast<uint16_t>(utf16Buffer[i]);

        if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (i + 1 >= utf16Buffer.size()) {
                utf8Result += '?';
                continue;
            }

            const uint16_t lo = static_cast<uint16_t>(utf16Buffer[i + 1]);
            if (lo < 0xDC00 || lo > 0xDFFF) {
                utf8Result += '?';
                continue;
            }

            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            ++i;
        }
        else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            utf8Result += '?';
            continue;
        }

        if (cp == '\t') {
            utf8Result += "\\t";
        }
        else if (cp == '\n') {
            utf8Result += "\\n";
        }
        else if (cp == '\r') {
            utf8Result += "\\r";
        }
        else if (cp < 0x20 || cp == 0x7F) {
            utf8Result += '?';
        }
        else {
            appendUtf8(cp);
        }
    }

    if (truncated) {
        utf8Result.append(kTruncatedSuffix);
    }

    return "\"" + utf8Result + "\"";
}
} // namespace

std::string DecodeManagedStringFromObject(uintptr_t objectPtr) {
    return Utf16ObjectToQuotedUtf8(objectPtr);
}

std::string DecodeManagedString(uintptr_t address) {
    uintptr_t managedStringPtr = 0;

    if (!Memory::TryReadValue(address, managedStringPtr) || managedStringPtr == 0) {
        return "null";
    }

    return Utf16ObjectToQuotedUtf8(managedStringPtr);
}

std::string DecodeRegisterArgument(const std::string& typeName, uintptr_t registerValue) {
    if (registerValue == 0) {
        using Cat = Types::TypeCategory;
        const Cat cat = Types::GetCategory(typeName);
        if (cat == Types::TypeCategory::STRING || cat == Types::TypeCategory::PTR) {
            return "null";
        }
    }

    using Cat = Types::TypeCategory;
    switch (Types::GetCategory(typeName)) {
    case Cat::I1: {
        const auto v = static_cast<int8_t>(registerValue & 0xFF);
        return std::to_string(v);
    }
    case Cat::I2: {
        const auto v = static_cast<int16_t>(registerValue & 0xFFFF);
        return std::to_string(v);
    }
    case Cat::I4: {
        const auto v = static_cast<int32_t>(registerValue & 0xFFFFFFFFu);
        return std::to_string(v);
    }
    case Cat::I8: {
        const auto v = static_cast<int64_t>(registerValue);
        return std::to_string(v);
    }
    case Cat::U1: {
        const auto v = static_cast<uint8_t>(registerValue & 0xFF);
        return std::to_string(v);
    }
    case Cat::U2: {
        const auto v = static_cast<uint16_t>(registerValue & 0xFFFF);
        return std::to_string(v);
    }
    case Cat::U4: {
        const auto v = static_cast<uint32_t>(registerValue & 0xFFFFFFFFu);
        return std::to_string(v);
    }
    case Cat::U8: {
        return std::to_string(registerValue);
    }
    case Cat::R4: {
        float v = 0.0f;
        const uint32_t bits = static_cast<uint32_t>(registerValue & 0xFFFFFFFFu);
        std::memcpy(&v, &bits, sizeof(v));
        char buf[32] = {};
        snprintf(buf, sizeof(buf), "%.4g", v);
        return buf;
    }
    case Cat::R8: {
        double v = 0.0;
        std::memcpy(&v, &registerValue, sizeof(v));
        char buf[32] = {};
        snprintf(buf, sizeof(buf), "%.6g", v);
        return buf;
    }
    case Cat::BOOLEAN: {
        const uint8_t v = static_cast<uint8_t>(registerValue & 0xFF);
        return v ? "true" : "false";
    }
    case Cat::STRING:
        return DecodeManagedStringFromObject(registerValue);
    case Cat::PTR: {
        char buf[32] = {};
        snprintf(buf, sizeof(buf), "0x%llX", static_cast<unsigned long long>(registerValue));
        return buf;
    }
    default:
        return "<?>";
    }
}

std::string DecodeFieldValue(const std::string& fieldType, uintptr_t valueAddress, bool hasValue) {
    if (!hasValue || !valueAddress) return "-";

    using Cat = Types::TypeCategory;
    switch (Types::GetCategory(fieldType)) {
    case Cat::I1: { int8_t  v; return Memory::TryReadValue(valueAddress, v) ? std::to_string(v) : "??"; }
    case Cat::I2: { int16_t v; return Memory::TryReadValue(valueAddress, v) ? std::to_string(v) : "??"; }
    case Cat::I4: { int32_t v; return Memory::TryReadValue(valueAddress, v) ? std::to_string(v) : "??"; }
    case Cat::I8: { int64_t v; return Memory::TryReadValue(valueAddress, v) ? std::to_string(v) : "??"; }
    case Cat::U1: { uint8_t  v; return Memory::TryReadValue(valueAddress, v) ? std::to_string(v) : "??"; }
    case Cat::U2: { uint16_t v; return Memory::TryReadValue(valueAddress, v) ? std::to_string(v) : "??"; }
    case Cat::U4: { uint32_t v; return Memory::TryReadValue(valueAddress, v) ? std::to_string(v) : "??"; }
    case Cat::U8: { uint64_t v; return Memory::TryReadValue(valueAddress, v) ? std::to_string(v) : "??"; }
    case Cat::R4: {
        float v;
        if (!Memory::TryReadValue(valueAddress, v)) return "??";
        char buf[32]; snprintf(buf, sizeof(buf), "%.4f", v); return buf;
    }
    case Cat::R8: {
        double v;
        if (!Memory::TryReadValue(valueAddress, v)) return "??";
        char buf[32]; snprintf(buf, sizeof(buf), "%.6f", v); return buf;
    }
    case Cat::BOOLEAN: {
        uint8_t v;
        return Memory::TryReadValue(valueAddress, v) ? (v ? "true" : "false") : "??";
    }
    case Cat::STRING:
        return DecodeManagedString(valueAddress);
    case Cat::VEC3: {
        // Vector3 is an inline value type: three consecutive floats at the
        // field address (no indirection, no managed object header).
        float x, y, z;
        if (!Memory::TryReadValue(valueAddress,              x)) return "??";
        if (!Memory::TryReadValue(valueAddress + sizeof(float), y)) return "??";
        if (!Memory::TryReadValue(valueAddress + 2 * sizeof(float), z)) return "??";
        char buf[64];
        snprintf(buf, sizeof(buf), "(%.3f, %.3f, %.3f)", x, y, z);
        return buf;
    }
    case Cat::PTR: {
        uintptr_t v;
        if (!Memory::TryReadValue(valueAddress, v)) return "??";
        if (v == 0) return "null";
        char buf[32]; snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)v); return buf;
    }
    case Cat::ARRAY: {
        // Shape preview: "T[N]" — the GUI uses this string both to label the
        // collection row and as the click target. Length lives at
        // UnityArrayLayout::LengthOffset; if the pointer is null or the
        // header isn't readable we fall through to "null" / "??" so the
        // renderer skips making the row clickable.
        uintptr_t arrayPtr = 0;
        if (!Memory::TryReadValue(valueAddress, arrayPtr)) return "??";
        if (arrayPtr == 0) return "null";
        size_t length = 0;
        if (!Memory::TryReadValue(arrayPtr + UnityArrayLayout::LengthOffset, length)) return "??";
        // Generous sanity ceiling: anything past ~100M elements is patently
        // a garbage read (would be 800MB+ of pointers) and we'd rather show
        // "??" than a 20-digit fantasy length. The dumper has its own,
        // tighter cap before it actually iterates the elements.
        if (length > 100'000'000) return "??";
        char buf[64];
        snprintf(buf, sizeof(buf), "%s[%zu]", fieldType.c_str(), length);
        return buf;
    }
    case Cat::LIST: {
        // For List<T> the wrapper itself is the field value (a managed object
        // pointer). The logical count lives in the _size field; the lookup of
        // _size's offset belongs in the dumper because it needs the klass
        // pointer. Here we only show the wrapper pointer + a "List<...>"
        // shape so the user knows it's drillable; the dumper view fills in
        // the real element list when the user clicks.
        uintptr_t listPtr = 0;
        if (!Memory::TryReadValue(valueAddress, listPtr)) return "??";
        if (listPtr == 0) return "null";
        char buf[96];
        snprintf(buf, sizeof(buf), "%s @ 0x%llX", fieldType.c_str(), (unsigned long long)listPtr);
        return buf;
    }
    default:
        return "-";
    }
}

namespace
{
void TrimWhitespaceInPlace(std::string& s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i]))
            != std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}
} // namespace

std::string StripQuotesForFieldEdit(std::string_view display) {
    if (display.size() >= 2 && display.front() == '"' && display.back() == '"') {
        return std::string(display.substr(1, display.size() - 2));
    }
    return std::string(display);
}

std::string NormalizeStringFieldInput(std::string_view raw) {
    std::string trimmed = StripQuotesForFieldEdit(raw);
    TrimWhitespaceInPlace(trimmed);
    if (trimmed.empty() || EqualsIgnoreCase(trimmed, "null")) {
        return {};
    }
    return trimmed;
}
} // namespace Engine::Decode

#endif
