#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "scripting/core/script_context.h"
#include "scripting/core/script_result.h"

namespace Scripting
{
enum class ScriptCommandStatus : uint8_t {
    Pending = 0,
    Queued,
    Running,
    Succeeded,
    Failed,
    CancelledQueueFull,
    CancelledTimeout,
    CancelledShutdown,
    DispatcherUnavailable,
    MainThreadNotCaptured,
    TimeoutEngineStarvation,
};

constexpr size_t kDefaultScriptCommandChannelCapacity = 32;

struct ScriptCommandResultSnapshot {
    ScriptCommandStatus status = ScriptCommandStatus::Pending;
    DS_Status resultStatus = DS_Status::DS_ERR_BAD_ARGUMENT;
};

// Shared waitable receipt for one Unity-touching script command.
struct ScriptCommand {
    mutable std::mutex mutex{};
    std::condition_variable cv{};
    std::atomic<ScriptCommandStatus> status{ScriptCommandStatus::Pending};
    std::atomic<bool> cancelRequested{false};
    DS_Status resultStatus = DS_Status::DS_ERR_BAD_ARGUMENT;

    bool IsTerminal() const noexcept;
    bool TryMarkRunning() noexcept;
    void MarkTerminal(ScriptCommandStatus terminalStatus, DS_Status result = DS_Status::DS_ERR_BAD_ARGUMENT) noexcept;
    ScriptCommandResultSnapshot GetResultSnapshot() const noexcept;
    void RequestCancel() noexcept;
    bool IsCancelled() const noexcept;
};

enum class DispatcherHealthState : uint8_t {
    Unavailable = 0,
    Uncaptured,
    Active,
    Starved,
};

struct DispatcherHealth {
    DispatcherHealthState state = DispatcherHealthState::Unavailable;
    bool isDispatchAvailable = false;
    bool isMainThreadCaptured = false;
    uint32_t queueDepth = 0;
    uint32_t droppedJobCount = 0;
    uint64_t lastDrainTickMs = 0;
    uint64_t drainAgeMs = 0;
    uint32_t capturedMainThreadId = 0;
    uint32_t inFlightCommandCount = 0;
};

// Bounded waitable command channel on top of MainThreadDispatcher.
class ScriptCommandChannel {
public:
    explicit ScriptCommandChannel(size_t capacity = kDefaultScriptCommandChannelCapacity);

    ScriptResult Submit(std::function<DS_Status()> task,
                        uint32_t timeoutMs,
                        CancellationState& cancel);

    void CancelAllPending(ScriptCommandStatus reason) noexcept;

private:
    void EndSubmit() noexcept;

    const size_t capacity_;
    std::atomic<size_t> inFlightCount_{0};
    std::mutex channelMutex_{};
    std::condition_variable channelCv_{};
    std::vector<std::shared_ptr<ScriptCommand>> activeCommands_{};
};

// Deadlock guard + waitable enqueue for Unity-touching work.
ScriptResult ExecuteUnityTask(std::function<DS_Status()> task,
                              uint32_t timeoutMs,
                              CancellationState& cancel);
ScriptResult ExecuteUnityTask(ScriptCommandChannel& channel,
                              std::function<DS_Status()> task,
                              uint32_t timeoutMs,
                              CancellationState& cancel);

DispatcherHealth QueryDispatcherHealth() noexcept;
DispatcherHealthState ResolveHealthState(const DispatcherHealth& health) noexcept;

DS_Status MapCommandStatusToDsStatus(ScriptCommandStatus status) noexcept;

} // namespace Scripting

#endif // ENABLE_DUMPER
