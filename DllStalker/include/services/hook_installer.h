#pragma once

#include <windows.h>

namespace Hooks
{
void EnsureMinHookInitialized();
void StartHooking();
bool InstallHook(LPVOID target, LPVOID detour, LPVOID* original, const char* name);
bool TryRegisterHookTarget(uintptr_t target);
void UnregisterHookTarget(uintptr_t target);
bool IsHookTargetRegistered(uintptr_t target);
} // namespace Hooks

// Convenience wrapper preserving the call-site shape used inside presets.
#define HOOK_FUNCTION(target, detour, original) \
    ::Hooks::InstallHook((LPVOID)(target), (LPVOID)(&detour), (LPVOID*)&(original), #detour)
