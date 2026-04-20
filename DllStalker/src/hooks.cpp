#include "pch.h"

#include "MinHook.h"

#include "hooks.h"
#include "hooks_definitions.h"
#include "unity_resolver.h"

#ifdef ENABLE_DUMPER
#include "unity_dumper.h"
#endif

namespace Hooks
{
namespace
{
// Legacy function to calculate absolute address from module base + offset
LPVOID getAddress(HMODULE hGame, uintptr_t offset)
{
    return (LPVOID)((uintptr_t)hGame + offset);
}

void HookInternal(LPVOID target, LPVOID detour, LPVOID* original, const char* name)
{
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK) {
        printf("[!] Failed to create hook: %s | Status: %d\n", name, status);
        return;
    }

    MH_STATUS enableStatus = MH_EnableHook(target);
    if (enableStatus != MH_OK) {
        printf("[!] Failed to enable hook: %s | Status: %d\n", name, enableStatus);
        return;
    }

    printf("[+] Successfully enabled: %s at %p\n", name, target);
}

#define HOOK_FUNCTION(target, detour, original) \
    HookInternal((LPVOID)(target), (LPVOID)(&detour), (LPVOID*)&(original), #detour)

namespace Presets
{
static void initGame(void* assImage)
{
	printf("[*] Initializing Game One Hooks...\n");
    HOOK_FUNCTION(Engine::Unity.GetMethodAddress(assImage, "SettingsManager", "Awake"),
        Dane::hkAwake, Dane::oAwake);
}

} // namespace Presets
} // namespace

void WorkInProgress(void* assImage);
void StartHooking(HMODULE hGame, void* assImage, const int option) {
    MH_Initialize();

    switch (option)
    {
        case 0:
            Presets::initGame(assImage);
            break;
        default:
			WorkInProgress(assImage);
            break;
    }
}

void WorkInProgress(void* assImage) {
    printf("[*] No preset selected. You can implement your own hooks in the WorkInProgress function.\n");
}

} // namespace Hooks
