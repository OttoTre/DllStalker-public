#include "pch.h"

#include "hooks_definition/dane.h"
#include "unity_resolver.h"

namespace Hooks
{
namespace Dane
{
namespace
{
constexpr bool HACK_ON = true;
} // namesapace

_Awake oAwake = nullptr;

void __fastcall hkAwake(void* __this) {
    oAwake(__this);

    if (__this) {
        const auto image = Engine::Unity.FindImage("Assembly-CSharp.dll");
        static uintptr_t hackOffset = Engine::Unity.GetFieldOffset(image, "SettingsManager", "hackMode");
        printf("[*] Awake Hook: Retrieved Offsets - hackMode: %p\n", (void*)hackOffset);

        if (hackOffset != 0) {
            *(bool*)((uintptr_t)__this + hackOffset) = HACK_ON;
        }

        printf("[+] Awake Hook: HackMode set to %s\n", HACK_ON ? "true" : "false");
    }
}
} // namespace Dane
} // namespace Hooks
