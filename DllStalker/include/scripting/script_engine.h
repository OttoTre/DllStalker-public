#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "scripting/bridge/script_command.h"
#include "scripting/bridge/script_reflection_api.h"
#include "scripting/core/script_audit.h"
#include "scripting/core/script_context.h"
#include "scripting/core/script_result.h"
#include "scripting/handles/script_handle_registry.h"
#include "scripting/runtime/script_runtime.h"

namespace Scripting
{
class ScriptEngine : public IScriptAuditSink {
public:
    ScriptEngine();
    ~ScriptEngine();

    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    DS_Status Start(const ScriptStartOptions& options);
    void RequestStop(ScriptStopReason reason = ScriptStopReason::UserRequest);
    void Pump();

    bool IsActive() const;
    ScriptRunState GetRunState() const;
    ScriptRuntimeResult GetLastResult() const;
    ScriptAuditSnapshot GetAuditSnapshot() const;
    void RecordAudit(const ScriptAuditRecord& record) noexcept override;

private:
    void WorkerMain(ScriptStartOptions options, ScriptInstanceId instanceId) noexcept;
    void LaunchWorkerLocked(const ScriptStartOptions& options);
    void ResetAudit() noexcept;

    mutable std::mutex mutex_;
    ScriptRunState runState_ = ScriptRunState::Idle;
    ScriptRuntimeResult lastResult_{};
    CancellationState cancellation_{};
    ScriptCommandChannel commandChannel_{};
    ScriptHandleRegistry handleRegistry_{};
    ScriptReflectionApi reflectionApi_;
    ScriptInstanceId instanceId_{};
    ScriptStopReason requestedStopReason_ = ScriptStopReason::None;
    std::optional<ScriptStartOptions> pendingStart_{};
    std::atomic<bool> workerFinished_{ false };
    std::atomic<bool> unresponsive_{ false };
    std::jthread worker_;

    mutable std::mutex auditMutex_{};
    ScriptAuditCounters auditCounters_{};
    ScriptAuditRingBuffer<> auditRecords_{};
    ScriptAuditRecord lastAuditRecord_{};
    uint64_t auditFirstEventTickMs_ = 0;
    uint64_t auditLastEventTickMs_ = 0;
};

} // namespace Scripting

#endif // ENABLE_DUMPER
