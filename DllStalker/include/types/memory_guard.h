#pragma once

#include <cstddef>
#include <cstdint>

#include <Windows.h>

// Process-memory readability/writability primitives plus typed read/write
// helpers built on top. Every dumper / GUI / walker / preset path that
// touches a raw uintptr_t into the target process funnels through here so an
// unmapped or guarded page surfaces as a clean false return instead of an SEH
// fault. Available in all build configs (including Release hook-only).
namespace Engine::Memory
{
bool IsReadablePointer(const void* ptr, size_t size);
bool IsWritablePointer(void* ptr, size_t size);
bool IsExecutablePointer(const void* ptr, size_t size = 1);
bool TryReadBytes(uintptr_t address, void* outBuffer, size_t size);
bool TryWriteBytes(uintptr_t address, const void* data, size_t size);

template <typename T>
bool TryReadValue(uintptr_t address, T& outValue) {
    return TryReadBytes(address, &outValue, sizeof(T));
}

template <typename T>
bool TryWriteValue(uintptr_t address, const T& value) {
    if (address % alignof(T) != 0) return false;
    return TryWriteBytes(address, &value, sizeof(T));
}
} // namespace Engine::Memory
