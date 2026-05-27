#include "pch.h"

#ifdef ENABLE_DUMPER

#include "types/value_decoder.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "types/memory_guard.h"
#include "types/type_classifier.h"

namespace Engine::Decode
{
namespace
{
constexpr int32_t UNITY_STRING_LAYOUT_OFFSET = 0x10; // Offset to the length field
constexpr int32_t UNITY_STRING_BUFFER_OFFSET = 0x14; // Offset to the UTF-16 buffer
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
    if (length > 2048) {
        length = 2048;
    }

    std::vector<wchar_t> utf16Buffer(static_cast<size_t>(length));
    const size_t byteSize = static_cast<size_t>(length) * sizeof(wchar_t);

    if (!Memory::IsReadablePointer(reinterpret_cast<void*>(managedStringPtr + UNITY_STRING_BUFFER_OFFSET),
                                  byteSize)) {
        return "\"<unreadable>\"";
    }

    std::memcpy(utf16Buffer.data(),
                reinterpret_cast<void*>(managedStringPtr + UNITY_STRING_BUFFER_OFFSET),
                byteSize);

    std::string utf8Result;
    utf8Result.reserve(static_cast<size_t>(length));

    for (wchar_t wc : utf16Buffer) {
        if (wc < 0x80) {
            utf8Result += static_cast<char>(wc);
        }
        else if (wc < 0x800) {
            utf8Result += static_cast<char>(0xC0 | (wc >> 6));
            utf8Result += static_cast<char>(0x80 | (wc & 0x3F));
        }
        else {
            utf8Result += static_cast<char>(0xE0 | (wc >> 12));
            utf8Result += static_cast<char>(0x80 | ((wc >> 6) & 0x3F));
            utf8Result += static_cast<char>(0x80 | (wc & 0x3F));
        }
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
    case Cat::PTR: {
        uintptr_t v;
        if (!Memory::TryReadValue(valueAddress, v)) return "??";
        if (v == 0) return "null";
        char buf[32]; snprintf(buf, sizeof(buf), "0x%llX", (unsigned long long)v); return buf;
    }
    case Cat::ARRAY: {
        // Shape preview: "T[N]" — the GUI uses this string both to label the
        // collection row and as the click target. Length lives at offset
        // 0x18 in the Il2CppArray / MonoArray header on x64; if the pointer
        // is null or the header isn't readable we fall through to "null" /
        // "??" so the renderer skips making the row clickable.
        uintptr_t arrayPtr = 0;
        if (!Memory::TryReadValue(valueAddress, arrayPtr)) return "??";
        if (arrayPtr == 0) return "null";
        constexpr uintptr_t kArrayLengthOffset = 0x18;
        if (!Memory::IsReadablePointer(reinterpret_cast<void*>(arrayPtr + kArrayLengthOffset), sizeof(size_t))) {
            return "??";
        }
        size_t length = 0;
        std::memcpy(&length, reinterpret_cast<void*>(arrayPtr + kArrayLengthOffset), sizeof(size_t));
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
} // namespace Engine::Decode

#endif
