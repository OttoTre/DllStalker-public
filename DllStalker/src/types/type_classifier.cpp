#include "pch.h"

#ifdef ENABLE_DUMPER

#include "types/type_classifier.h"
#include "types/known_unity_types.h"

#include <unordered_map>

namespace Engine::Types
{
namespace
{
// Substring helpers operate on already-lowercased buffers to keep the
// match cheap. Both checks must run before the generic '*' / '.' PTR
// heuristic so we don't tag arrays / lists as opaque pointers.
constexpr std::string_view kListPrefix = "system.collections.generic.list";

bool LooksLikeArray(std::string_view lowered) {
    return lowered.size() >= 2
        && lowered[lowered.size() - 2] == '['
        && lowered[lowered.size() - 1] == ']';
}

bool LooksLikeList(std::string_view lowered) {
    return lowered.substr(0, kListPrefix.size()) == kListPrefix;
}

// Unity (and System.Char) value-types that contain '.' but must not become PTR.
// Checked after scalar map / array / list, before the PTR heuristic.
// Prefer shared table; keep this as a thin fallback.
TypeCategory LookupAllowlistedValueType(std::string_view lowered) {
    if (const KnownUnityType* known = FindKnownUnityTypeLowered(lowered)) {
        if (IsInlineValueStruct(known->category)) {
            return known->category;
        }
    }
    return TypeCategory::UNKNOWN;
}

TypeCategory ClassifyLowered(std::string_view lowered) {
    struct SvHash {
        using is_transparent = void;
        size_t operator()(std::string_view sv) const noexcept { return std::hash<std::string_view>{}(sv); }
        size_t operator()(const std::string& s) const noexcept { return std::hash<std::string_view>{}(s); }
    };
    struct SvEqual {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept { return a == b; }
    };

    // Short aliases + non-System spellings stay local; System.*/UnityEngine.*
    // come from kKnownUnityTypes.
    static const std::unordered_map<std::string, TypeCategory, SvHash, SvEqual> aliasMap = {
        {"int8", TypeCategory::I1}, {"sbyte", TypeCategory::I1},
        {"int16", TypeCategory::I2}, {"short", TypeCategory::I2},
        {"int", TypeCategory::I4}, {"int32", TypeCategory::I4},
        {"long", TypeCategory::I8}, {"int64", TypeCategory::I8},
        {"uint8", TypeCategory::U1}, {"byte", TypeCategory::U1},
        {"uint16", TypeCategory::U2}, {"ushort", TypeCategory::U2},
        {"char", TypeCategory::U2},
        {"uint32", TypeCategory::U4}, {"uint", TypeCategory::U4},
        {"uint64", TypeCategory::U8}, {"ulong", TypeCategory::U8},
        {"float", TypeCategory::R4}, {"single", TypeCategory::R4},
        {"double", TypeCategory::R8},
        {"bool", TypeCategory::BOOLEAN}, {"boolean", TypeCategory::BOOLEAN},
        {"string", TypeCategory::STRING}
    };

    auto aliasIt = aliasMap.find(lowered);
    if (aliasIt != aliasMap.end()) return aliasIt->second;

    if (const KnownUnityType* known = FindKnownUnityTypeLowered(lowered)) {
        return known->category;
    }

    if (LooksLikeArray(lowered)) return TypeCategory::ARRAY;
    if (LooksLikeList(lowered))  return TypeCategory::LIST;

    const TypeCategory allowlisted = LookupAllowlistedValueType(lowered);
    if (allowlisted != TypeCategory::UNKNOWN) return allowlisted;

    // Namespaced reference types (UnityEngine.GameObject, etc.) stay PTR.
    // Do not delete this heuristic — only allowlist value-types above it.
    if (lowered.find('*') != std::string_view::npos || lowered.find('.') != std::string_view::npos)
        return TypeCategory::PTR;

    return TypeCategory::UNKNOWN;
}
} // namespace

TypeCategory GetCategory(std::string_view type) {
    auto toLower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; };

    if (type.size() < 64) {
        char localBuf[64];
        for (size_t i = 0; i < type.size(); ++i) localBuf[i] = toLower(type[i]);
        return ClassifyLowered(std::string_view(localBuf, type.size()));
    }

    std::string lowerBuf;
    lowerBuf.reserve(type.size());
    for (char c : type) lowerBuf += toLower(c);
    return ClassifyLowered(lowerBuf);
}
} // namespace Engine::Types

#endif
