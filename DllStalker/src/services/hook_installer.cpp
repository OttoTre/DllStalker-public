#include "pch.h"

#include "services/hook_installer.h"

#include "MinHook.h"

#include "presets/hook_preset.h"
#ifdef ENABLE_DUMPER
#include "types/memory_guard.h"
#endif

#include <mutex>
#include <unordered_set>

namespace Hooks
{
namespace
{
constexpr const char* kSelectedPresetName = "-";

std::once_flag g_minHookInitOnce;
std::mutex     g_registryMutex;
std::unordered_set<uintptr_t> g_hookedTargets;
} // namespace

void EnsureMinHookInitialized() {
    std::call_once(g_minHookInitOnce, []() { MH_Initialize(); });
}

bool TryRegisterHookTarget(uintptr_t target) {
    if (target == 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_registryMutex);
    return g_hookedTargets.insert(target).second;
}

void UnregisterHookTarget(uintptr_t target) {
    if (target == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_registryMutex);
    g_hookedTargets.erase(target);
}

bool IsHookTargetRegistered(uintptr_t target) {
    if (target == 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_registryMutex);
    return g_hookedTargets.find(target) != g_hookedTargets.end();
}

bool InstallHook(LPVOID target, LPVOID detour, LPVOID* original, const char* name) {
    if (!target) {
        printf("[!] Hook target is null: %s\n", name ? name : "<unnamed>");
        return false;
    }

#ifdef ENABLE_DUMPER
    if (!Engine::Memory::IsExecutablePointer(target)) {
        printf("[!] Hook target not executable: %s at %p\n", name ? name : "<unnamed>", target);
        return false;
    }
#endif

    const uintptr_t targetAddr = reinterpret_cast<uintptr_t>(target);
    if (!TryRegisterHookTarget(targetAddr)) {
        printf("[!] Hook target already registered: %s at %p\n", name ? name : "<unnamed>", target);
        return false;
    }

    EnsureMinHookInitialized();

    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK) {
        UnregisterHookTarget(targetAddr);
        printf("[!] Failed to create hook: %s | Status: %d\n", name, status);
        return false;
    }

    MH_STATUS enableStatus = MH_EnableHook(target);
    if (enableStatus != MH_OK) {
        MH_RemoveHook(target);
        UnregisterHookTarget(targetAddr);
        printf("[!] Failed to enable hook: %s | Status: %d\n", name, enableStatus);
        return false;
    }

    printf("[+] Successfully enabled: %s at %p\n", name, target);
    return true;
}

void StartHooking() {
    EnsureMinHookInitialized();
    Presets::InstallSelected(kSelectedPresetName);
}
} // namespace Hooks
