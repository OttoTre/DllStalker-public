#include "pch.h"

#ifdef ENABLE_DUMPER

#include "scripting/bridge/script_command.h"

#include "scripting/core/script_error_boundary.h"
#include "services/main_thread_dispatcher.h"

#include <algorithm>
#include <chrono>

namespace Scripting
{
namespace
{
constexpr uint64_t kStarvationThresholdMs = 2000;

struct TaskContext {
    std::function<DS_Status()>* task = nullptr;
};

DS_Status __cdecl RunTaskContext(void* context) noexcept {
    auto* ctx = static_cast<TaskContext*>(context);
    if (!ctx || !ctx->task || !*ctx->task) {
        return DS_Status::DS_ERR_BAD_ARGUMENT;
    }
    return (*ctx->task)();
}

ScriptResult RunTaskInline(std::function<DS_Status()>& task) noexcept {
    TaskContext ctx{&task};
    unsigned long sehCode = 0;
    const DS_Status status = RunSehProtected(RunTaskContext, &ctx, sehCode);
    if (IsOk(status)) {
        return ScriptResult::Ok();
    }
    return ScriptResult::Fail(status);
}

bool IsTerminalStatus(ScriptCommandStatus status) noexcept {
    switch (status) 
    {
    case ScriptCommandStatus::Pending:
    case ScriptCommandStatus::Queued:
    case ScriptCommandStatus::Running:
        return false;
    default:
        return true;
    }
}

} // namespace

bool ScriptCommand::IsTerminal() const noexcept {
    return IsTerminalStatus(status.load(std::memory_order_acquire));
}

bool ScriptCommand::TryMarkRunning() noexcept {
    std::lock_guard<std::mutex> lock(mutex);
    if (cancelRequested.load(std::memory_order_acquire)) {
        return false;
    }
    if (status.load(std::memory_order_relaxed) != ScriptCommandStatus::Queued) {
        return false;
    }
    status.store(ScriptCommandStatus::Running, std::memory_order_release);
    return true;
}

void ScriptCommand::MarkTerminal(ScriptCommandStatus terminalStatus, DS_Status result) noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (IsTerminalStatus(status.load(std::memory_order_relaxed))) {
            return;
        }
        status.store(terminalStatus, std::memory_order_release);
        resultStatus = result;
    }
    cv.notify_all();
}

ScriptCommandResultSnapshot ScriptCommand::GetResultSnapshot() const noexcept {
    ScriptCommandResultSnapshot snapshot{};
    std::lock_guard<std::mutex> lock(mutex);
    snapshot.status = status.load(std::memory_order_relaxed);
    snapshot.resultStatus = resultStatus;
    return snapshot;
}

void ScriptCommand::RequestCancel() noexcept {
    cancelRequested.store(true, std::memory_order_release);
}

bool ScriptCommand::IsCancelled() const noexcept {
    return cancelRequested.load(std::memory_order_acquire);
}

ScriptCommandChannel::ScriptCommandChannel(size_t capacity)
    : capacity_(capacity > 0 ? capacity : kDefaultScriptCommandChannelCapacity) {}

ScriptResult ScriptCommandChannel::Submit(std::function<DS_Status()> task,
                                          uint32_t timeoutMs,
                                          CancellationState& cancel) {
    if (!task) {
        return ScriptResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT);
    }
    if (cancel.IsCancelled()) {
        return ScriptResult::Fail(DS_Status::DS_ERR_CANCELLED);
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    {
        std::unique_lock<std::mutex> lock(channelMutex_);
        while (inFlightCount_ >= capacity_) {
            if (cancel.IsCancelled()) {
                return ScriptResult::Fail(DS_Status::DS_ERR_CANCELLED);
            }
            if (channelCv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                return ScriptResult::Fail(DS_Status::DS_ERR_QUEUE_FULL);
            }
        }
        ++inFlightCount_;
    }

    auto command = std::make_shared<ScriptCommand>();
    {
        std::lock_guard<std::mutex> lock(channelMutex_);
        activeCommands_.push_back(command);
    }

    std::weak_ptr<ScriptCommand> weakCommand = command;

    auto dispatcherJob = [weakCommand, task = std::move(task)]() mutable {
        const auto commandLocked = weakCommand.lock();
        if (!commandLocked) {
            return;
        }

        if (!commandLocked->TryMarkRunning()) {
            if (!commandLocked->IsTerminal()) {
                commandLocked->MarkTerminal(ScriptCommandStatus::CancelledShutdown,
                                              DS_Status::DS_ERR_CANCELLED);
            }
            return;
        }

        if (commandLocked->IsCancelled()) {
            commandLocked->MarkTerminal(ScriptCommandStatus::CancelledTimeout,
                                        DS_Status::DS_ERR_CANCELLED);
            return;
        }

        const ScriptResult inlineResult = RunTaskInline(task);
        if (IsOk(inlineResult.status)) {
            commandLocked->MarkTerminal(ScriptCommandStatus::Succeeded, inlineResult.status);
        } else {
            commandLocked->MarkTerminal(ScriptCommandStatus::Failed, inlineResult.status);
        }
    };

    command->status.store(ScriptCommandStatus::Queued, std::memory_order_release);

    if (!Engine::Services::MainThreadDispatcher::TryEnqueueNoDrop(std::move(dispatcherJob))) {
        {
            std::lock_guard<std::mutex> lock(channelMutex_);
            activeCommands_.erase(
                std::remove(activeCommands_.begin(), activeCommands_.end(), command),
                activeCommands_.end());
        }
        EndSubmit();
        command->MarkTerminal(ScriptCommandStatus::CancelledQueueFull,
                              DS_Status::DS_ERR_QUEUE_FULL);
        return ScriptResult::Fail(DS_Status::DS_ERR_QUEUE_FULL);
    }

    std::unique_lock<std::mutex> waitLock(command->mutex);
    const bool completed = command->cv.wait_until(waitLock, deadline, [&] {
        return command->IsTerminal();
    });
    waitLock.unlock();

    ScriptResult result = ScriptResult::Ok();
    if (!completed) {
        command->RequestCancel();
        const DispatcherHealth health = QueryDispatcherHealth();
        if (ResolveHealthState(health) == DispatcherHealthState::Starved) {
            command->MarkTerminal(ScriptCommandStatus::TimeoutEngineStarvation,
                                  DS_Status::DS_ERR_STARVATION);
            result = ScriptResult::Fail(DS_Status::DS_ERR_STARVATION);
        } else {
            command->MarkTerminal(ScriptCommandStatus::CancelledTimeout,
                                  DS_Status::DS_ERR_TIMEOUT);
            result = ScriptResult::Fail(DS_Status::DS_ERR_TIMEOUT);
        }
    } else {
        const ScriptCommandResultSnapshot snapshot = command->GetResultSnapshot();
        if (snapshot.status == ScriptCommandStatus::Succeeded) {
            result = ScriptResult::Ok();
        } else {
            result = ScriptResult::Fail(MapCommandStatusToDsStatus(snapshot.status));
            if (snapshot.resultStatus != DS_Status::DS_OK &&
                snapshot.resultStatus != DS_Status::DS_ERR_BAD_ARGUMENT) {
                result.status = snapshot.resultStatus;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(channelMutex_);
        activeCommands_.erase(
            std::remove(activeCommands_.begin(), activeCommands_.end(), command),
            activeCommands_.end());
    }
    EndSubmit();
    return result;
}

void ScriptCommandChannel::CancelAllPending(ScriptCommandStatus reason) noexcept {
    std::vector<std::shared_ptr<ScriptCommand>> snapshot;
    {
        std::lock_guard<std::mutex> lock(channelMutex_);
        snapshot = activeCommands_;
    }

    for (const auto& command : snapshot) {
        if (!command || command->IsTerminal()) {
            continue;
        }
        command->RequestCancel();
        command->MarkTerminal(reason, DS_Status::DS_ERR_CANCELLED);
    }
}

void ScriptCommandChannel::EndSubmit() noexcept {
    {
        std::lock_guard<std::mutex> lock(channelMutex_);
        if (inFlightCount_ > 0) {
            --inFlightCount_;
        }
    }
    channelCv_.notify_all();
}

ScriptResult ExecuteUnityTask(std::function<DS_Status()> task,
                              uint32_t timeoutMs,
                              CancellationState& cancel) {
    static ScriptCommandChannel s_defaultChannel{};
    return ExecuteUnityTask(s_defaultChannel, std::move(task), timeoutMs, cancel);
}

ScriptResult ExecuteUnityTask(ScriptCommandChannel& channel,
                              std::function<DS_Status()> task,
                              uint32_t timeoutMs,
                              CancellationState& cancel) {
    if (!task) {
        return ScriptResult::Fail(DS_Status::DS_ERR_BAD_ARGUMENT);
    }
    if (cancel.IsCancelled()) {
        return ScriptResult::Fail(DS_Status::DS_ERR_CANCELLED);
    }
    if (!Engine::Services::MainThreadDispatcher::IsDispatchAvailable()) {
        return ScriptResult::Fail(DS_Status::DS_ERR_DISPATCHER_UNAVAILABLE);
    }
    if (!Engine::Services::MainThreadDispatcher::IsMainThreadCaptured()) {
        return ScriptResult::Fail(DS_Status::DS_ERR_MAIN_THREAD_NOT_CAPTURED);
    }
    if (Engine::Services::MainThreadDispatcher::IsOnMainThread()) {
        return RunTaskInline(task);
    }

    return channel.Submit(std::move(task), timeoutMs, cancel);
}

DispatcherHealth QueryDispatcherHealth() noexcept {
    DispatcherHealth health{};
    health.isDispatchAvailable = Engine::Services::MainThreadDispatcher::IsDispatchAvailable();
    health.isMainThreadCaptured = Engine::Services::MainThreadDispatcher::IsMainThreadCaptured();
    health.queueDepth = Engine::Services::MainThreadDispatcher::GetQueueDepth();
    health.droppedJobCount = Engine::Services::MainThreadDispatcher::GetDroppedJobCount();
    health.lastDrainTickMs = Engine::Services::MainThreadDispatcher::GetLastDrainTickMs();
    health.capturedMainThreadId = Engine::Services::MainThreadDispatcher::GetMainThreadId();

    const uint64_t now = GetTickCount64();
    if (health.lastDrainTickMs > 0 && now >= health.lastDrainTickMs) {
        health.drainAgeMs = now - health.lastDrainTickMs;
    }

    health.state = ResolveHealthState(health);
    return health;
}

DispatcherHealthState ResolveHealthState(const DispatcherHealth& health) noexcept {
    if (!health.isDispatchAvailable) {
        return DispatcherHealthState::Unavailable;
    }
    if (!health.isMainThreadCaptured) {
        return DispatcherHealthState::Uncaptured;
    }
    if (health.lastDrainTickMs == 0 || health.drainAgeMs > kStarvationThresholdMs) {
        return DispatcherHealthState::Starved;
    }
    return DispatcherHealthState::Active;
}

DS_Status MapCommandStatusToDsStatus(ScriptCommandStatus status) noexcept {
    switch (status) {
    case ScriptCommandStatus::Succeeded:
        return DS_Status::DS_OK;
    case ScriptCommandStatus::CancelledQueueFull:
        return DS_Status::DS_ERR_QUEUE_FULL;
    case ScriptCommandStatus::CancelledTimeout:
        return DS_Status::DS_ERR_TIMEOUT;
    case ScriptCommandStatus::CancelledShutdown:
        return DS_Status::DS_ERR_CANCELLED;
    case ScriptCommandStatus::DispatcherUnavailable:
        return DS_Status::DS_ERR_DISPATCHER_UNAVAILABLE;
    case ScriptCommandStatus::MainThreadNotCaptured:
        return DS_Status::DS_ERR_MAIN_THREAD_NOT_CAPTURED;
    case ScriptCommandStatus::TimeoutEngineStarvation:
        return DS_Status::DS_ERR_STARVATION;
    case ScriptCommandStatus::Failed:
        return DS_Status::DS_ERR_BAD_ARGUMENT;
    default:
        return DS_Status::DS_ERR_BAD_ARGUMENT;
    }
}

} // namespace Scripting

#endif // ENABLE_DUMPER
