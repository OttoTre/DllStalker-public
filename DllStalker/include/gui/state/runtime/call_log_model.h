#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "services/call_log_types.h"
#include "types/dumper_types.h"

namespace Gui::State
{
struct CallLogModel {
    static constexpr size_t kMaxHooks = 16;
    static constexpr size_t kMaxLines = 500;

    mutable std::deque<std::string>      lines{};
    mutable std::deque<Engine::Services::CallLogEvent> pendingEvents{};
    std::vector<Engine::Services::CallLogHookSpec> hooks{};
    mutable std::mutex                   mutex{};
    uint32_t                             nextHookId = 1;
    bool                                 callbackRegistered = false;

    void EnsureLineCallbackRegistered();

    void PushEvent(const Engine::Services::CallLogEvent& event);
    void PushLine(const char* line);
    void ClearLines();
    std::vector<std::string> SnapshotLines() const;

    bool IsLogging(uintptr_t target) const;

    enum class ToggleResult {
        Added,
        Removed,
        RejectedCap,
        RejectedIneligible,
        RejectedAlreadyHooked,
        RejectedInstallFailed,
    };

    ToggleResult Toggle(const Engine::MethodInfo& method, const std::string& className);
    bool         RemoveHook(uint32_t hookId);

    static bool IsLogEligible(const Engine::MethodInfo& method);
};
} // namespace Gui::State

#endif // ENABLE_DUMPER
