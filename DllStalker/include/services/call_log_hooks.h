#pragma once

#include "pch.h"

#ifdef ENABLE_DUMPER

#include <cstddef>
#include <cstdint>

#include "services/call_log_types.h"

namespace Engine::Services::CallLogHooks
{
constexpr size_t kMaxSlots = 16;

enum class InstallResult {
    Ok,
    TargetNull,
    TargetAlreadyHooked,
    NoFreeSlot,
    MinHookFailed,
};

using LineCallback = void (*)(void* userData, const char* line);

void SetLineCallback(LineCallback callback, void* userData);

bool IsTargetHooked(uintptr_t target);

InstallResult Install(CallLogHookSpec& inOutSpec);

bool Uninstall(uint32_t hookId);

bool UninstallByTarget(uintptr_t target);
} // namespace Engine::Services::CallLogHooks

#endif // ENABLE_DUMPER
