#pragma once

#ifdef ENABLE_DUMPER

#include <atomic>
#include <cstdint>
#include <string>

#include "scripting/core/script_audit.h"
#include "scripting/core/script_context.h"
#include "scripting/core/script_result.h"
#include "scripting/core/script_sandbox.h"

namespace Scripting
{
class ScriptHandleRegistry;
class ScriptReflectionApi;
class ScriptCommandChannel;

constexpr int kScriptCallbackNoRef = -2;

enum class ScriptRunState : uint8_t {
    Idle = 0,
    Running,
    Stopping,
    Reloading,
    Completed,
    Cancelled,
    Failed,
    LoadError,
    Unresponsive,
    Quarantined,
};

enum class ScriptStopReason : uint8_t {
    None = 0,
    UserRequest,
    Shutdown,
};

struct ScriptStartOptions {
    std::wstring relativeScriptPath;
    std::wstring resolvedEntryPath;
    std::wstring packageRoot;
    ScriptProfile profile = ScriptProfile::Safe;
    IOutputSink* outputSink = nullptr;
    uint32_t instructionBudget = 100000;
    uint32_t tickIntervalMs = 16;
    uint32_t commandTimeoutMs = 1500;
    uint32_t softTimeoutMs = 2000;
    uint32_t hardQuarantineMs = 5000;
};

struct ScriptRuntimeResult {
    ScriptRunState runState = ScriptRunState::Idle;
    DS_Status status = DS_Status::DS_OK;
    ScriptStopReason stopReason = ScriptStopReason::None;
    std::string message;
};

// Opaque per-run host context passed into the LuaJIT runtime implementation.
struct LuaScriptHostContext {
    ScriptInstanceId instanceId{};
    CancellationState* cancellation = nullptr;
    IOutputSink* outputSink = nullptr;
    SandboxPolicy sandboxPolicy{};
    std::wstring packageRoot;
    std::wstring entryFilePath;
    uint32_t instructionBudget = 100000;
    uint32_t tickIntervalMs = 16;
    uint32_t commandTimeoutMs = 1500;
    uint32_t softTimeoutMs = 2000;
    uint32_t hardQuarantineMs = 5000;
    int tickCallbackRef = kScriptCallbackNoRef;
    int unloadCallbackRef = kScriptCallbackNoRef;
    ScriptHandleRegistry* handleRegistry = nullptr;
    ScriptReflectionApi* reflectionApi = nullptr;
    ScriptCommandChannel* commandChannel = nullptr;
    IScriptAuditSink* auditSink = nullptr;
    std::atomic<bool>* unresponsiveFlag = nullptr;
    uint64_t lastTickStartedMs = 0;
    uint64_t lastTickDurationMs = 0;
    uint64_t lastSoftWarnTickMs = 0;
    uint64_t unloadDeadlineTickMs = 0;
    bool quarantineRequested = false;
    bool isUnloading = false;
};

// Run one script file on the current thread. Caller must tag the thread as ours when invoked
// from a script worker. Creates, runs, and closes the isolated lua_State on this thread.
ScriptRuntimeResult RunScriptOnCurrentThread(const LuaScriptHostContext& hostContext);

} // namespace Scripting

#endif // ENABLE_DUMPER
