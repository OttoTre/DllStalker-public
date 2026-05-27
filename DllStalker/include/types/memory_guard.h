#pragma once

#ifdef ENABLE_DUMPER

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <Windows.h>

// Process-memory readability/writability primitives plus typed read/write
// helpers built on top. Every dumper / GUI / walker path that touches a raw
// uintptr_t into the target process funnels through here so an unmapped or
// guarded page surfaces as a clean false return instead of an SEH fault.
namespace Engine::Memory
{
bool IsReadablePointer(const void* ptr, size_t size);
bool IsWritablePointer(void* ptr, size_t size);

template <typename T>
bool TryReadValue(uintptr_t address, T& outValue) {
    if (!address) return false;
    if (!IsReadablePointer(reinterpret_cast<void*>(address), sizeof(T))) return false;
    // memcpy is safe for unaligned memory and usually optimized away
    std::memcpy(&outValue, reinterpret_cast<void*>(address), sizeof(T));
    return true;
}

template <typename T>
bool TryWriteValue(uintptr_t address, const T& value) {
    if (!address || (address % alignof(T) != 0)) return false;
    if (!IsWritablePointer(reinterpret_cast<void*>(address), sizeof(T))) return false;
    *reinterpret_cast<T*>(address) = value;
    return true;
}
} // namespace Engine::Memory

#endif // ENABLE_DUMPER
