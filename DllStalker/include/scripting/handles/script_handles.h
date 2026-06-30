#pragma once

#ifdef ENABLE_DUMPER

#include <cstdint>

namespace Scripting
{
using ScriptHandle = uint64_t;

enum class ScriptHandleKind : uint8_t {
    Invalid = 0,
    Image,
    Class,
    Method,
    Field,
    Instance,
};

enum class RuntimeKind : uint8_t {
    Unknown = 0,
    Il2Cpp,
    Mono,
    Native,
};

constexpr ScriptHandle kInvalidScriptHandle = 0;

constexpr ScriptHandle PackHandle(uint32_t registryIndex, uint32_t generation) noexcept {
    return (static_cast<uint64_t>(generation) << 32) | registryIndex;
}

constexpr uint32_t GetRegistryIndex(ScriptHandle handle) noexcept {
    return static_cast<uint32_t>(handle & 0xFFFFFFFFu);
}

constexpr uint32_t GetHandleGeneration(ScriptHandle handle) noexcept {
    return static_cast<uint32_t>(handle >> 32);
}

constexpr bool IsValidScriptHandle(ScriptHandle handle) noexcept {
    return handle != kInvalidScriptHandle;
}

} // namespace Scripting

#endif // ENABLE_DUMPER
