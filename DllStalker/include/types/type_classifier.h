#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <string>
#include <string_view>

// Maps a managed type name (e.g. "System.Int32", "Stat[]",
// "System.Collections.Generic.List`1<Item>") to a coarse category that the
// dumper / GUI use to pick a decode, write, or navigation strategy. Pure
// string analysis -- no runtime engine calls.
//
// Enums: type names are usually dotted (e.g. "Game.Team") and would hit the
// PTR heuristic. Do NOT rely on GetCategory for enum identity — use
// FieldInfo::isEnum (+ underlyingType) for decode/write/Search chips.
//
// Scalar / Unity inline kinds: System.* and UnityEngine.* live in
// known_unity_types.h. Keep decode + write + IsEditable paired when adding kinds.
namespace Engine::Types
{
enum class TypeCategory {
    UNKNOWN, I1, I2, I4, I8, U1, U2, U4, U8, R4, R8, BOOLEAN, STRING, PTR,
    // Container categories. Detected by the same name-based heuristics as
    // PTR (see GetCategory) but routed through the collection-view pipeline
    // in the dumper / GUI rather than treated as a single opaque pointer.
    ARRAY,  // T[] -- Il2CppArray / MonoArray header (length @ +0x18, elements @ +0x20)
    LIST,   // System.Collections.Generic.List`1<T> -- wrapper over T[] _items + int _size
    // Inline value-type structs decoded/written at the field address.
    // Allowlisted before the dotted-name → PTR heuristic.
    VEC2,   // UnityEngine.Vector2 -- x, y
    VEC3,   // UnityEngine.Vector3 -- x, y, z
    VEC4,   // UnityEngine.Vector4 -- x, y, z, w
    QUAT,   // UnityEngine.Quaternion -- x, y, z, w
    COLOR,  // UnityEngine.Color -- r, g, b, a (float)
    COLOR32,// UnityEngine.Color32 -- r, g, b, a (byte)
    RECT    // UnityEngine.Rect -- x, y, width, height (float)
};

TypeCategory GetCategory(std::string_view type);

// Inline Unity/System value structs at the field address (not PTR/array/list).
inline bool IsInlineValueStruct(TypeCategory cat) {
    switch (cat) {
    case TypeCategory::VEC2:
    case TypeCategory::VEC3:
    case TypeCategory::VEC4:
    case TypeCategory::QUAT:
    case TypeCategory::COLOR:
    case TypeCategory::COLOR32:
    case TypeCategory::RECT:
        return true;
    default:
        return false;
    }
}
} // namespace Engine::Types

#endif // ENABLE_DUMPER
