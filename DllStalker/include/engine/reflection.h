#pragma once

#include <cstdint>

#include "engine/unity_module.h"

// Name-based reflection helpers used by the hook installer and preset code.
// Returns native method pointers (absolute VA / Mono JIT) or field offsets
// the user can plug into MinHook. Walks the inheritance chain so callers
// don't have to know which type along the chain owns the symbol.
namespace Engine
{
namespace ReflectionDefaults
{
inline constexpr const char* kGlobalNamespace = "";
inline constexpr int kAnyAmount = -1;
} // namespace ReflectionDefaults

class Reflection
{
public:
    explicit Reflection(const UnityModule& module);

    // Single-pass inheritance walk: probes each class for both the literal
    // name and its property-getter form ("get_<name>") before climbing to
    // the parent. Returns 0 when nothing matches; callers log + skip.
    uintptr_t GetMethodAddress(void* image, const char* className, const char* methodName,
                               int args = ReflectionDefaults::kAnyAmount,
                               const char* ns = ReflectionDefaults::kGlobalNamespace) const;

    uintptr_t GetFieldOffset(void* image, const char* className, const char* fieldName,
                             const char* ns = ReflectionDefaults::kGlobalNamespace) const;

private:
    const UnityModule& m_module;
};
} // namespace Engine
