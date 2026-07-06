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
namespace Engine::Types
{
enum class TypeCategory {
    UNKNOWN, I1, I2, I4, I8, U1, U2, U4, U8, R4, R8, BOOLEAN, STRING, PTR,
    // Container categories. Detected by the same name-based heuristics as
    // PTR (see GetCategory) but routed through the collection-view pipeline
    // in the dumper / GUI rather than treated as a single opaque pointer.
    ARRAY,  // T[] -- Il2CppArray / MonoArray header (length @ +0x18, elements @ +0x20)
    LIST,   // System.Collections.Generic.List`1<T> -- wrapper over T[] _items + int _size
    // Inline value-type structs decoded directly from the field address.
    VEC3    // UnityEngine.Vector3 -- three consecutive floats (x, y, z)
};

TypeCategory GetCategory(std::string_view type);
} // namespace Engine::Types

#endif // ENABLE_DUMPER
