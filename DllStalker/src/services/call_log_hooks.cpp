#include "pch.h"

#ifdef ENABLE_DUMPER

#include "services/call_log_hooks.h"

#include "services/call_log_types.h"
#include "services/hook_installer.h"

#include "MinHook.h"

#include "types/type_classifier.h"
#include "types/value_decoder.h"

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

constexpr size_t kMaxParamTypes = 4;
constexpr size_t kTypeNameBytes = 96;
constexpr size_t kLabelBytes    = 128;
constexpr size_t kLineBytes     = 1024;

struct SlotState {
    std::atomic<bool> active{false};
    uint32_t          hookId   = 0;
    uintptr_t         target   = 0;
    bool              isStatic = false;
    uint8_t           paramCount = 0;
    char              displayLabel[kLabelBytes]{};
    char              paramTypeNames[kMaxParamTypes][kTypeNameBytes]{};
    FastCall4         original = nullptr;
};

SlotState g_slots[kMaxSlots]{};

std::mutex    g_installMutex;
LineCallback  g_lineCallback = nullptr;
void*         g_lineUserData   = nullptr;

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

void AppendArg(char* dest,
               size_t destSize,
               size_t& linePos,
               size_t& argsWritten,
               const char* name,
               const char* value) {
    if (!dest || destSize == 0 || linePos >= destSize) {
        return;
    }
    const int added = snprintf(dest + linePos,
                               destSize - linePos,
                               argsWritten == 0 ? "%s: %s" : ", %s: %s",
                               name,
                               value ? value : "<?>");
    if (added > 0) {
        linePos += static_cast<size_t>(added);
        ++argsWritten;
    }
}

__declspec(noinline) void FormatAndPushLine(int slotIndex, void* rcx, void* rdx, void* r8, void* r9) {
    const SlotState& slot = g_slots[slotIndex];
    if (!slot.active.load(std::memory_order_acquire)) {
        return;
    }

    void* argRegs[4] = {rcx, rdx, r8, r9};
    const uint8_t maxArgs = slot.isStatic ? 4 : 3;

    char timeBuf[32] = {};
    WriteLocalTimeLabel(timeBuf, sizeof(timeBuf));

    char line[kLineBytes] = {};
    size_t linePos = snprintf(line,
                              sizeof(line),
                              "[%s] %s(",
                              timeBuf,
                              slot.displayLabel[0] != '\0' ? slot.displayLabel : "?");

    char argName[16] = {};
    size_t argsWritten = 0;
    const uint8_t argCount = static_cast<uint8_t>((std::min)(static_cast<size_t>(slot.paramCount),
                                                          static_cast<size_t>(maxArgs)));
    for (uint8_t i = 0; i < argCount && i < kMaxParamTypes; ++i) {
        snprintf(argName, sizeof(argName), "arg%u", i);
        const int regIndex = slot.isStatic ? static_cast<int>(i) : static_cast<int>(i) + 1;
        if (regIndex < 0 || regIndex > 3) {
            continue;
        }
        const uintptr_t regVal = reinterpret_cast<uintptr_t>(argRegs[regIndex]);
        const std::string typeName = slot.paramTypeNames[i];
        const std::string decoded  = Engine::Decode::DecodeRegisterArgument(typeName, regVal);
        AppendArg(line, sizeof(line), linePos, argsWritten, argName, decoded.c_str());
    }

    if (linePos < sizeof(line) - 2) {
        strncat_s(line, sizeof(line), ")", _TRUNCATE);
    }

    if (g_lineCallback) {
        g_lineCallback(g_lineUserData, line);
    }
}

__declspec(noinline) void* OnHit(int slotIndex, void* rcx, void* rdx, void* r8, void* r9) {
    SlotState& slot = g_slots[slotIndex];
    FastCall4    original = slot.original;
    if (!original) {
        return nullptr;
    }

    if (slot.active.load(std::memory_order_acquire)) {
        FormatAndPushLine(slotIndex, rcx, rdx, r8, r9);
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
    slot.paramCount  = static_cast<uint8_t>((std::min)(spec.paramTypes.size(), kMaxParamTypes));
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

void SetLineCallback(LineCallback callback, void* userData) {
    std::lock_guard<std::mutex> lock(g_installMutex);
    g_lineCallback = callback;
    g_lineUserData = userData;
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

bool UninstallByTarget(uintptr_t target) {
    std::lock_guard<std::mutex> lock(g_installMutex);
    for (int i = 0; i < static_cast<int>(kMaxSlots); ++i) {
        if (g_slots[i].active.load(std::memory_order_acquire) && g_slots[i].target == target) {
            return UninstallSlotLocked(i);
        }
    }
    return false;
}
} // namespace Engine::Services::CallLogHooks

#endif // ENABLE_DUMPER
