#include "pch.h"

#ifdef ENABLE_DUMPER

#include "types/memory_guard.h"

namespace Engine::Memory
{
bool IsReadablePointer(const void* ptr, size_t size) {
    if (!ptr || size == 0) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    constexpr DWORD noAccess = PAGE_NOACCESS | PAGE_GUARD;
    if (mbi.Protect & noAccess) return false;
    constexpr DWORD readMask = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                               PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & readMask) != 0;
}

bool IsWritablePointer(void* ptr, size_t size) {
    if (!ptr || size == 0) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    constexpr DWORD writeMask = PAGE_READWRITE | PAGE_WRITECOPY |
                                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & writeMask) != 0;
}
} // namespace Engine::Memory

#endif
