#include "pch.h"

#ifdef ENABLE_DUMPER

#include "services/call_log_hooks.h"

#include "services/call_log_types.h"
#include "services/hook_installer.h"

#include "MinHook.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

namespace Engine::Services::CallLogHooks
{
namespace
{
using FastCall4 = void* (__fastcall*)(void*, void*, void*, void*);

struct SlotState {
    std::atomic<bool> active{false};
    uint32_t          hookId   = 0;
    uintptr_t         target   = 0;
    bool              isStatic = false;
    uint8_t           paramCount = 0;
    char              displayLabel[kCallLogLabelBytes]{};
    char              paramTypeNames[kCallLogMaxParamTypes][kCallLogTypeNameBytes]{};
    FastCall4         original = nullptr;
};

SlotState g_slots[kMaxSlots]{};

std::mutex    g_installMutex;
EventCallback g_eventCallback = nullptr;
void*         g_eventUserData  = nullptr;

void WriteLocalTimeLabel(char* buf, size_t bufSize) {
    if (!buf || bufSize == 0) {
        return;
    }
    buf[0] = '\0';
    const time_t now = time(nullptr);
    tm           localTime{};
    localtime_s(&localTime, &now);
    strftime(buf, bufSize, "%H:%M:%S", &localTime);
}

__declspec(noinline) void CaptureAndPushEvent(int slotIndex, void* rcx, void* rdx, void* r8, void* r9) {
    const SlotState& slot = g_slots[slotIndex];
    if (!slot.active.load(std::memory_order_acquire)) {
        return;
    }

    CallLogEvent event{};
    event.hookId = slot.hookId;
    event.isStatic = slot.isStatic;
    event.paramCount = slot.paramCount;
    WriteLocalTimeLabel(event.timeLabel, sizeof(event.timeLabel));
    strncpy_s(event.displayLabel, slot.displayLabel, _TRUNCATE);

    for (uint8_t i = 0; i < slot.paramCount && i < kCallLogMaxParamTypes; ++i) {
        strncpy_s(event.paramTypeNames[i], slot.paramTypeNames[i], _TRUNCATE);
    }

    event.argRegisters[0] = reinterpret_cast<uintptr_t>(rcx);
    event.argRegisters[1] = reinterpret_cast<uintptr_t>(rdx);
    event.argRegisters[2] = reinterpret_cast<uintptr_t>(r8);
    event.argRegisters[3] = reinterpret_cast<uintptr_t>(r9);

    if (g_eventCallback) {
        g_eventCallback(g_eventUserData, event);
    }
}

__declspec(noinline) void* OnHit(int slotIndex, void* rcx, void* rdx, void* r8, void* r9) {
    SlotState& slot = g_slots[slotIndex];
    FastCall4    original = slot.original;
    if (!original) {
        return nullptr;
    }

    if (slot.active.load(std::memory_order_acquire)) {
        CaptureAndPushEvent(slotIndex, rcx, rdx, r8, r9);
    }

    __try {
        return original(rcx, rdx, r8, r9);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

#define DEFINE_DETOUR_SLOT(N) \
    void* __fastcall DetourSlot##N(void* rcx, void* rdx, void* r8, void* r9) { \
        return OnHit(N, rcx, rdx, r8, r9); \
    }

DEFINE_DETOUR_SLOT(0)
DEFINE_DETOUR_SLOT(1)
DEFINE_DETOUR_SLOT(2)
DEFINE_DETOUR_SLOT(3)
DEFINE_DETOUR_SLOT(4)
DEFINE_DETOUR_SLOT(5)
DEFINE_DETOUR_SLOT(6)
DEFINE_DETOUR_SLOT(7)
DEFINE_DETOUR_SLOT(8)
DEFINE_DETOUR_SLOT(9)
DEFINE_DETOUR_SLOT(10)
DEFINE_DETOUR_SLOT(11)
DEFINE_DETOUR_SLOT(12)
DEFINE_DETOUR_SLOT(13)
DEFINE_DETOUR_SLOT(14)
DEFINE_DETOUR_SLOT(15)

#undef DEFINE_DETOUR_SLOT

using DetourFn = void* (__fastcall*)(void*, void*, void*, void*);

DetourFn DetourForSlot(int index) {
    static DetourFn table[] = {
        DetourSlot0,  DetourSlot1,  DetourSlot2,  DetourSlot3,
        DetourSlot4,  DetourSlot5,  DetourSlot6,  DetourSlot7,
        DetourSlot8,  DetourSlot9,  DetourSlot10, DetourSlot11,
        DetourSlot12, DetourSlot13, DetourSlot14, DetourSlot15,
    };
    if (index < 0 || index >= static_cast<int>(kMaxSlots)) {
        return nullptr;
    }
    return table[index];
}

void CopySpecToSlot(SlotState& slot, const CallLogHookSpec& spec) {
    const std::string label = spec.DisplayLabel();
    strncpy_s(slot.displayLabel, label.c_str(), _TRUNCATE);
    slot.isStatic    = spec.isStatic;
    slot.hookId      = spec.hookId;
    slot.target      = spec.target;
    slot.paramCount  = static_cast<uint8_t>((std::min)(spec.paramTypes.size(), kCallLogMaxParamTypes));
    for (uint8_t i = 0; i < slot.paramCount; ++i) {
        strncpy_s(slot.paramTypeNames[i], spec.paramTypes[i].typeName.c_str(), _TRUNCATE);
    }
}

int FindSlotByHookId(uint32_t hookId) {
    for (int i = 0; i < static_cast<int>(kMaxSlots); ++i) {
        if (g_slots[i].active.load(std::memory_order_acquire) && g_slots[i].hookId == hookId) {
            return i;
        }
    }
    return -1;
}

int FindFreeSlot() {
    for (int i = 0; i < static_cast<int>(kMaxSlots); ++i) {
        if (!g_slots[i].active.load(std::memory_order_acquire)) {
            return i;
        }
    }
    return -1;
}

bool UninstallSlotLocked(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= static_cast<int>(kMaxSlots)) {
        return false;
    }

    SlotState& slot = g_slots[slotIndex];
    if (!slot.active.load(std::memory_order_acquire)) {
        return false;
    }

    slot.active.store(false, std::memory_order_release);

    if (slot.target != 0) {
        MH_DisableHook(reinterpret_cast<LPVOID>(slot.target));
        MH_RemoveHook(reinterpret_cast<LPVOID>(slot.target));
        Hooks::UnregisterHookTarget(slot.target);
    }

    slot.original      = nullptr;
    slot.target        = 0;
    slot.hookId        = 0;
    slot.paramCount    = 0;
    slot.displayLabel[0] = '\0';
    return true;
}
} // namespace

void SetEventCallback(EventCallback callback, void* userData) {
    std::lock_guard<std::mutex> lock(g_installMutex);
    g_eventCallback = callback;
    g_eventUserData = userData;
}

bool IsTargetHooked(uintptr_t target) {
    return Hooks::IsHookTargetRegistered(target);
}

InstallResult Install(CallLogHookSpec& inOutSpec) {
    if (inOutSpec.target == 0) {
        return InstallResult::TargetNull;
    }
    if (Hooks::IsHookTargetRegistered(inOutSpec.target)) {
        return InstallResult::TargetAlreadyHooked;
    }

    std::lock_guard<std::mutex> lock(g_installMutex);

    if (Hooks::IsHookTargetRegistered(inOutSpec.target)) {
        return InstallResult::TargetAlreadyHooked;
    }

    const int slotIndex = FindFreeSlot();
    if (slotIndex < 0) {
        return InstallResult::NoFreeSlot;
    }

    DetourFn detour = DetourForSlot(slotIndex);
    if (!detour) {
        return InstallResult::MinHookFailed;
    }

    FastCall4 original = nullptr;
    char hookName[160] = {};
    snprintf(hookName,
             sizeof(hookName),
             "CallLog_%s_%s",
             inOutSpec.className.c_str(),
             inOutSpec.methodName.c_str());

    if (!Hooks::InstallHook(reinterpret_cast<LPVOID>(inOutSpec.target),
                            reinterpret_cast<LPVOID>(detour),
                            reinterpret_cast<LPVOID*>(&original),
                            hookName)) {
        return InstallResult::MinHookFailed;
    }

    SlotState& slot = g_slots[slotIndex];
    slot.active.store(false, std::memory_order_release);
    slot.original = original;
    CopySpecToSlot(slot, inOutSpec);
    slot.active.store(true, std::memory_order_release);

    inOutSpec.slotIndex = slotIndex;
    return InstallResult::Ok;
}

bool Uninstall(uint32_t hookId) {
    std::lock_guard<std::mutex> lock(g_installMutex);
    const int slotIndex = FindSlotByHookId(hookId);
    if (slotIndex < 0) {
        return false;
    }
    return UninstallSlotLocked(slotIndex);
}
} // namespace Engine::Services::CallLogHooks

#endif // ENABLE_DUMPER
