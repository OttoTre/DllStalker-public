#include "pch.h"

#ifdef ENABLE_DUMPER

#include "types/type_classifier.h"

#include <unordered_map>

namespace Engine::Types
{
namespace
{
// Substring helpers operate on already-lowercased buffers to keep the
// match cheap. Both checks must run before the generic '*' / '.' PTR
// heuristic so we don't tag arrays / lists as opaque pointers.
constexpr std::string_view kListPrefix = "system.collections.generic.list";

// Array detection: any type whose lowercased name ends in "[]". Catches
// "Stat[]", "System.Int32[]", "UnityEngine.GameObject[]", etc.
bool LooksLikeArray(std::string_view lowered) {
    return lowered.size() >= 2
        && lowered[lowered.size() - 2] == '['
        && lowered[lowered.size() - 1] == ']';
}

// List<T> detection by canonical name prefix. We accept both
// "System.Collections.Generic.List`1<T>" and the bare
// "System.Collections.Generic.List<T>" because IL2CPP and Mono format the
// generic suffix differently across builds.
bool LooksLikeList(std::string_view lowered) {
    return lowered.substr(0, kListPrefix.size()) == kListPrefix;
}
} // namespace

TypeCategory GetCategory(std::string_view type) {
    struct SvHash {
        using is_transparent = void;
        size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }
        size_t operator()(const std::string& s) const noexcept { return std::hash<std::string_view>{}(s); }
    };
    struct SvEqual {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
    };

    static const std::unordered_map<std::string, TypeCategory, SvHash, SvEqual> categoryMap = {
        {"int8", TypeCategory::I1}, {"sbyte", TypeCategory::I1}, {"system.sbyte", TypeCategory::I1},
        {"int16", TypeCategory::I2}, {"short", TypeCategory::I2}, {"system.int16", TypeCategory::I2},
        {"int", TypeCategory::I4}, {"int32", TypeCategory::I4}, {"system.int32", TypeCategory::I4},
        {"long", TypeCategory::I8}, {"int64", TypeCategory::I8}, {"system.int64", TypeCategory::I8},
        {"uint8", TypeCategory::U1}, {"byte", TypeCategory::U1}, {"system.byte", TypeCategory::U1},
        {"uint16", TypeCategory::U2}, {"ushort", TypeCategory::U2}, {"system.uint16", TypeCategory::U2},
        {"uint32", TypeCategory::U4}, {"uint", TypeCategory::U4}, {"system.uint32", TypeCategory::U4},
        {"uint64", TypeCategory::U8}, {"ulong", TypeCategory::U8}, {"system.uint64", TypeCategory::U8},
        {"float", TypeCategory::R4}, {"single", TypeCategory::R4}, {"system.single", TypeCategory::R4},
        {"double", TypeCategory::R8}, {"system.double", TypeCategory::R8},
        {"bool", TypeCategory::BOOLEAN}, {"boolean", TypeCategory::BOOLEAN}, {"system.boolean", TypeCategory::BOOLEAN},
        {"string", TypeCategory::STRING}, {"system.string", TypeCategory::STRING}
    };

    auto toLower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; };

    if (type.size() < 64) {
        char localBuf[64];
        for (size_t i = 0; i < type.size(); ++i) localBuf[i] = toLower(type[i]);

        std::string_view sv(localBuf, type.size());
        auto it = categoryMap.find(sv);
        if (it != categoryMap.end()) return it->second;

        if (LooksLikeArray(sv)) return TypeCategory::ARRAY;
        if (LooksLikeList(sv))  return TypeCategory::LIST;

        if (sv == "unityengine.vector3") return TypeCategory::VEC3;

        if (sv.find('*') != std::string_view::npos || sv.find('.') != std::string_view::npos)
            return TypeCategory::PTR;
    }
    else {
        std::string lowerBuf;
        lowerBuf.reserve(type.size());
        for (char c : type) lowerBuf += toLower(c);

        auto it = categoryMap.find(lowerBuf);
        if (it != categoryMap.end()) return it->second;

        std::string_view sv(lowerBuf);
        if (LooksLikeArray(sv)) return TypeCategory::ARRAY;
        if (LooksLikeList(sv))  return TypeCategory::LIST;

        if (sv == "unityengine.vector3") return TypeCategory::VEC3;

        if (lowerBuf.find('*') != std::string::npos || lowerBuf.find('.') != std::string::npos)
            return TypeCategory::PTR;
    }

    return TypeCategory::UNKNOWN;
}
} // namespace Engine::Types

#endif
