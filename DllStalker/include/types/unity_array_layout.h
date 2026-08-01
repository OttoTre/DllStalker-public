#pragma once

#include <cstdint>

// Shared Unity x64 array header offsets. IL2CPP Il2CppArray and Mono MonoArray
// use the same layout on x64: length at +0x18, first element at +0x20.
namespace Engine::UnityArrayLayout
{
constexpr uintptr_t LengthOffset   = 0x18;
constexpr uintptr_t ElementsOffset = 0x20;
} // namespace Engine::UnityArrayLayout
