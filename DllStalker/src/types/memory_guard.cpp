#include "pch.h"

#include "types/memory_guard.h"

#include <cstring>

namespace Engine::Memory
{
namespace
{
bool HasReadableProtection(DWORD protect) {
    constexpr DWORD noAccess = PAGE_NOACCESS | PAGE_GUARD;
    if (protect & noAccess) {
        return false;
    }

    constexpr DWORD readMask = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                               PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (protect & readMask) != 0;
}

bool HasWritableProtection(DWORD protect) {
    constexpr DWORD noAccess = PAGE_NOACCESS | PAGE_GUARD;
    if (protect & noAccess) {
        return false;
    }

    constexpr DWORD writeMask = PAGE_READWRITE | PAGE_WRITECOPY |
                                PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (protect & writeMask) != 0;
}

bool HasExecutableProtection(DWORD protect) {
    constexpr DWORD noAccess = PAGE_NOACCESS | PAGE_GUARD;
    if (protect & noAccess) {
        return false;
    }

    constexpr DWORD execMask = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                               PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (protect & execMask) != 0;
}

bool ValidateRange(const void* ptr, size_t size, bool (*protectionAllowed)(DWORD)) {
    if (!ptr || size == 0) {
        return false;
    }

    const uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t end = start + size;
    if (end <= start) {
        return false;
    }

    uintptr_t current = start;
    while (current < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<const void*>(current), &mbi, sizeof(mbi)) == 0) {
            return false;
        }

        if (mbi.State != MEM_COMMIT) {
            return false;
        }

        if (!protectionAllowed(mbi.Protect)) {
            return false;
        }

        const uintptr_t regionStart = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t regionEnd = regionStart + mbi.RegionSize;
        if (regionEnd <= current || regionEnd < regionStart) {
            return false;
        }

        current = regionEnd;
    }

    return true;
}

__declspec(noinline) bool SehCopy(void* dst, const void* src, size_t size) noexcept {
    __try {
        std::memcpy(dst, src, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
} // namespace

bool IsReadablePointer(const void* ptr, size_t size) {
    return ValidateRange(ptr, size, HasReadableProtection);
}

bool IsWritablePointer(void* ptr, size_t size) {
    return ValidateRange(ptr, size, HasWritableProtection);
}

bool IsExecutablePointer(const void* ptr, size_t size) {
    return ValidateRange(ptr, size, HasExecutableProtection);
}

bool TryReadBytes(uintptr_t address, void* outBuffer, size_t size) {
    if (!address || !outBuffer || size == 0) {
        return false;
    }

    const void* src = reinterpret_cast<const void*>(address);
    if (!IsReadablePointer(src, size)) {
        return false;
    }

    return SehCopy(outBuffer, src, size);
}

bool TryWriteBytes(uintptr_t address, const void* data, size_t size) {
    if (!address || !data || size == 0) {
        return false;
    }

    void* dst = reinterpret_cast<void*>(address);
    if (!IsWritablePointer(dst, size)) {
        return false;
    }

    return SehCopy(dst, data, size);
}
} // namespace Engine::Memory
