#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstddef>
#include <string_view>

#include "types/type_classifier.h"

// Single owner table for canonical System.* / UnityEngine.* scalar + inline
// value-types. Classifier keeps short aliases separately; SDK export and
// GetCategory(system.*) share this table.
namespace Engine::Types
{
struct KnownUnityType {
    const char* name;       // canonical casing, e.g. "System.Int32"
    const char* lowered;    // lowercase for classifier lookup
    TypeCategory category;
    const char* cppType;    // SDK C++ spelling
    size_t      sizeBytes;  // layout size used by padded structs
};

inline constexpr KnownUnityType kKnownUnityTypes[] = {
    { "System.Boolean",         "system.boolean",         TypeCategory::BOOLEAN, "bool",       1 },
    { "System.Byte",            "system.byte",            TypeCategory::U1,      "uint8_t",    1 },
    { "System.SByte",           "system.sbyte",           TypeCategory::I1,      "int8_t",     1 },
    { "System.Int16",           "system.int16",           TypeCategory::I2,      "int16_t",    2 },
    { "System.UInt16",          "system.uint16",          TypeCategory::U2,      "uint16_t",   2 },
    { "System.Char",            "system.char",            TypeCategory::U2,      "uint16_t",   2 },
    { "System.Int32",           "system.int32",           TypeCategory::I4,      "int32_t",    4 },
    { "System.UInt32",          "system.uint32",          TypeCategory::U4,      "uint32_t",   4 },
    { "System.Int64",           "system.int64",           TypeCategory::I8,      "int64_t",    8 },
    { "System.UInt64",          "system.uint64",          TypeCategory::U8,      "uint64_t",   8 },
    { "System.Single",          "system.single",          TypeCategory::R4,      "float",      4 },
    { "System.Double",          "system.double",          TypeCategory::R8,      "double",     8 },
    { "System.IntPtr",          "system.intptr",          TypeCategory::PTR,     "void*",      8 },
    { "System.UIntPtr",         "system.uintptr",         TypeCategory::PTR,     "void*",      8 },
    { "System.String",          "system.string",          TypeCategory::STRING,  "void*",      8 },
    { "System.Object",          "system.object",          TypeCategory::PTR,     "void*",      8 },
    { "UnityEngine.Vector2",    "unityengine.vector2",    TypeCategory::VEC2,    "float[2]",   8 },
    { "UnityEngine.Vector3",    "unityengine.vector3",    TypeCategory::VEC3,    "float[3]",   12 },
    { "UnityEngine.Vector4",    "unityengine.vector4",    TypeCategory::VEC4,    "float[4]",   16 },
    { "UnityEngine.Quaternion", "unityengine.quaternion", TypeCategory::QUAT,    "float[4]",   16 },
    { "UnityEngine.Color",      "unityengine.color",      TypeCategory::COLOR,   "float[4]",   16 },
    { "UnityEngine.Color32",    "unityengine.color32",    TypeCategory::COLOR32, "uint8_t[4]", 4 },
    { "UnityEngine.Rect",       "unityengine.rect",       TypeCategory::RECT,    "float[4]",   16 },
};

inline const KnownUnityType* FindKnownUnityTypeLowered(std::string_view lowered) {
    for (const KnownUnityType& entry : kKnownUnityTypes) {
        if (lowered == entry.lowered) {
            return &entry;
        }
    }
    return nullptr;
}

inline const KnownUnityType* FindKnownUnityTypeExact(std::string_view name) {
    for (const KnownUnityType& entry : kKnownUnityTypes) {
        if (name == entry.name) {
            return &entry;
        }
    }
    return nullptr;
}
} // namespace Engine::Types

#endif // ENABLE_DUMPER
