#include "pch.h"

#ifdef ENABLE_DUMPER

extern "C" {
#include <luajit/lauxlib.h>
#include <luajit/lua.h>
}

#include <atomic>
#include <chrono>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "scripting/bridge/lua_host_context.h"
#include "scripting/runtime/luajit_runtime_internal.h"

namespace Scripting
{
namespace
{
constexpr const char kCancellationError[] = "script cancelled";

ScriptRuntimeResult MakeResult(ScriptRunState runState, DS_Status status, std::string message) {
    ScriptRuntimeResult result{};
    result.runState = runState;
    result.status = status;
    result.message = std::move(message);
    return result;
}

bool IsCancellationMessage(const char* message) {
    return message != nullptr && std::string_view(message) == kCancellationError;
}

bool HasCallback(int callbackRef) noexcept {
    return callbackRef != kScriptCallbackNoRef;
}

void InstructionHook(lua_State* state, lua_Debug* /*debug*/) {
    LuaScriptHostContext* hostContext = GetHostContext(state);
    if (hostContext == nullptr) {
        return;
    }

    const uint64_t now = GetTickCount64();
    if (hostContext->isUnloading && hostContext->unloadDeadlineTickMs != 0 &&
        now > hostContext->unloadDeadlineTickMs) {
        luaL_error(state, "script unload timeout");
    }

    if (!hostContext->isUnloading &&
        hostContext->hardQuarantineMs > 0 &&
        hostContext->lastTickStartedMs != 0 &&
        now >= hostContext->lastTickStartedMs &&
        now - hostContext->lastTickStartedMs > hostContext->hardQuarantineMs) {
        hostContext->quarantineRequested = true;
        if (hostContext->cancellation != nullptr) {
            hostContext->cancellation->RequestCancel();
        }
        if (hostContext->auditSink != nullptr) {
            ScriptAuditRecord record{};
            record.timestamp = now;
            record.scriptInstanceId = hostContext->instanceId;
            record.kind = ScriptAuditKind::Timeout;
            record.status = DS_Status::DS_ERR_TIMEOUT;
            hostContext->auditSink->RecordAudit(record);
        }
        luaL_error(state, "script quarantined");
    }

    if (!hostContext->isUnloading &&
        hostContext->cancellation != nullptr && hostContext->cancellation->IsCancelled()) {
        luaL_error(state, kCancellationError);
    }
}

ScriptRuntimeResult MakeCallbackErrorResult(ScriptRunState state,
                                            DS_Status status,
                                            lua_State* luaState,
                                            const char* fallbackMessage) {
    const char* errorMessage = lua_tolstring(luaState, -1, nullptr);
    std::string message = errorMessage ? errorMessage : fallbackMessage;
    lua_pop(luaState, 1);
    return MakeResult(state, status, std::move(message));
}

ScriptRuntimeResult InvokeCallback(lua_State* state,
                                   LuaScriptHostContext& hostContext,
                                   int callbackRef,
                                   ScriptRunState failureState,
                                   const char* fallbackMessage) {
    if (!HasCallback(callbackRef)) {
        return MakeResult(ScriptRunState::Completed, DS_Status::DS_OK, {});
    }

    lua_rawgeti(state, LUA_REGISTRYINDEX, callbackRef);
    const uint64_t start = GetTickCount64();
    hostContext.lastTickStartedMs = start;

    const int status = lua_pcall(state, 0, 0, 0);
    const uint64_t end = GetTickCount64();
    hostContext.lastTickDurationMs = end >= start ? end - start : 0;

    if (status == 0) {
        if (!hostContext.isUnloading &&
            hostContext.hardQuarantineMs > 0 &&
            hostContext.lastTickDurationMs > hostContext.hardQuarantineMs) {
            if (hostContext.unresponsiveFlag != nullptr) {
                hostContext.unresponsiveFlag->store(false, std::memory_order_release);
            }
            hostContext.quarantineRequested = true;
            if (hostContext.cancellation != nullptr) {
                hostContext.cancellation->RequestCancel();
            }
            if (hostContext.auditSink != nullptr) {
                ScriptAuditRecord record{};
                record.timestamp = end;
                record.scriptInstanceId = hostContext.instanceId;
                record.kind = ScriptAuditKind::Timeout;
                record.status = DS_Status::DS_ERR_TIMEOUT;
                hostContext.auditSink->RecordAudit(record);
            }
            return MakeResult(ScriptRunState::Quarantined,
                              DS_Status::DS_ERR_TIMEOUT,
                              "script tick exceeded hard quarantine budget");
        }

        const bool overSoftBudget =
            !hostContext.isUnloading &&
            hostContext.softTimeoutMs > 0 &&
            hostContext.lastTickDurationMs > hostContext.softTimeoutMs;
        if (hostContext.unresponsiveFlag != nullptr) {
            hostContext.unresponsiveFlag->store(overSoftBudget, std::memory_order_release);
        }
        if (overSoftBudget && hostContext.outputSink != nullptr) {
            const bool shouldWarn =
                hostContext.lastSoftWarnTickMs == 0 ||
                end - hostContext.lastSoftWarnTickMs >= 1000;
            if (shouldWarn) {
                hostContext.outputSink->Append("[warn] script tick exceeded soft budget");
                hostContext.lastSoftWarnTickMs = end;
            }
        } else if (!overSoftBudget) {
            hostContext.lastSoftWarnTickMs = 0;
        }
        return MakeResult(ScriptRunState::Completed, DS_Status::DS_OK, {});
    }

    const char* errorMessage = lua_tolstring(state, -1, nullptr);
    if (hostContext.quarantineRequested) {
        return MakeCallbackErrorResult(ScriptRunState::Quarantined,
                                       DS_Status::DS_ERR_TIMEOUT,
                                       state,
                                       fallbackMessage);
    }
    if ((hostContext.cancellation != nullptr && hostContext.cancellation->IsCancelled()) ||
        IsCancellationMessage(errorMessage)) {
        return MakeCallbackErrorResult(ScriptRunState::Cancelled,
                                       DS_Status::DS_ERR_CANCELLED,
                                       state,
                                       fallbackMessage);
    }
    return MakeCallbackErrorResult(failureState, DS_Status::DS_ERR_BAD_ARGUMENT, state, fallbackMessage);
}
} // namespace

void InstallCancellationHook(lua_State* state, uint32_t instructionBudget) {
    if (instructionBudget == 0) {
        instructionBudget = 1;
    }
    lua_sethook(state, InstructionHook, LUA_MASKCOUNT, static_cast<int>(instructionBudget));
}

void ClearCancellationHook(lua_State* state) {
    lua_sethook(state, nullptr, 0, 0);
}

void UnrefCallback(lua_State* state, int& callbackRef) {
    if (HasCallback(callbackRef)) {
        luaL_unref(state, LUA_REGISTRYINDEX, callbackRef);
        callbackRef = kScriptCallbackNoRef;
    }
}

ScriptRuntimeResult InvokeUnload(lua_State* state, LuaScriptHostContext& hostContext) {
    if (!HasCallback(hostContext.unloadCallbackRef)) {
        return MakeResult(ScriptRunState::Completed, DS_Status::DS_OK, {});
    }

    hostContext.isUnloading = true;
    hostContext.unloadDeadlineTickMs = GetTickCount64() + 250;
    ScriptRuntimeResult result = InvokeCallback(state,
                                                hostContext,
                                                hostContext.unloadCallbackRef,
                                                ScriptRunState::Failed,
                                                "unload callback failed");
    hostContext.isUnloading = false;
    hostContext.unloadDeadlineTickMs = 0;
    return result;
}

ScriptRuntimeResult RunTickLoop(lua_State* state, LuaScriptHostContext& hostContext) {
    if (!HasCallback(hostContext.tickCallbackRef)) {
        return MakeResult(ScriptRunState::Completed, DS_Status::DS_OK, {});
    }

    const uint32_t intervalMs = hostContext.tickIntervalMs == 0 ? 16 : hostContext.tickIntervalMs;
    while (hostContext.cancellation == nullptr || !hostContext.cancellation->IsCancelled()) {
        const ScriptRuntimeResult tickResult =
            InvokeCallback(state, hostContext, hostContext.tickCallbackRef, ScriptRunState::Failed, "tick failed");
        if (tickResult.runState == ScriptRunState::Failed ||
            tickResult.runState == ScriptRunState::Cancelled ||
            tickResult.runState == ScriptRunState::Quarantined) {
            return tickResult;
        }

        if (hostContext.cancellation != nullptr) {
            if (hostContext.cancellation->WaitForCancelOrTimeout(std::chrono::milliseconds(intervalMs))) {
                break;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
        }
    }

    return MakeResult(ScriptRunState::Cancelled, DS_Status::DS_ERR_CANCELLED, kCancellationError);
}

} // namespace Scripting

#endif // ENABLE_DUMPER
