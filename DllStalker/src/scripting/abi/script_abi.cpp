#include "pch.h"

#ifdef ENABLE_DUMPER

#include "scripting/abi/script_abi.h"

#include <cstring>
#include <functional>
#include <memory>
#include <utility>

#include "scripting/bridge/script_command.h"
#include "scripting/bridge/script_reflection_api.h"
#include "scripting/core/script_audit.h"
#include "scripting/runtime/script_runtime.h"
#include "types/memory_guard.h"

namespace
{
constexpr uint32_t kDsAbiFeatureFlags =
    DS_ABI_FEATURE_INSTANCE_PRIMITIVES |
    DS_ABI_FEATURE_SINGLE_PROXY_DLL;

constexpr const char kDsAbiBuildId[] = "DllStalker Curated ABI v1";
} // namespace

namespace Scripting
{
namespace
{
thread_local LuaScriptHostContext* t_currentAbiContext = nullptr;

bool AddOffset(uintptr_t base, uint32_t offset, uintptr_t& outAddress) noexcept {
    const uintptr_t value = base + static_cast<uintptr_t>(offset);
    if (value < base) {
        return false;
    }
    outAddress = value;
    return true;
}

LuaScriptHostContext* RequireAbiContext() noexcept {
    LuaScriptHostContext* hostContext = t_currentAbiContext;
    if (hostContext == nullptr ||
        hostContext->cancellation == nullptr ||
        hostContext->reflectionApi == nullptr) {
        return nullptr;
    }
    return hostContext;
}

void RecordAbiAudit(LuaScriptHostContext& hostContext,
                    ScriptAuditKind kind,
                    DS_Status status,
                    ScriptHandle targetHandle,
                    uint32_t offset,
                    uint64_t payloadValue = 0) noexcept {
    if (hostContext.auditSink == nullptr) {
        return;
    }

    ScriptAuditRecord record{};
    record.timestamp = GetTickCount64();
    record.scriptInstanceId = hostContext.instanceId;
    record.targetHandle = targetHandle;
    record.fieldOffsetOrMethodToken = offset;
    record.kind = kind;
    record.status = status;
    record.payloadValue = payloadValue;
    hostContext.auditSink->RecordAudit(record);
}

ScriptResult ExecuteAbiTask(LuaScriptHostContext& hostContext,
                            std::function<DS_Status()> task) {
    if (hostContext.commandChannel != nullptr) {
        return ExecuteUnityTask(*hostContext.commandChannel,
                                std::move(task),
                                hostContext.commandTimeoutMs,
                                *hostContext.cancellation);
    }

    return ExecuteUnityTask(std::move(task),
                            hostContext.commandTimeoutMs,
                            *hostContext.cancellation);
}

DS_Status ResolveInstanceAddress(LuaScriptHostContext& hostContext,
                                 ScriptHandle scriptHandle,
                                 uintptr_t& outInstance) noexcept {
    return hostContext.reflectionApi
        ->ValidateInstanceHandle(hostContext.instanceId, scriptHandle, outInstance)
        .status;
}

// Heap-owned results for queued main-thread work. Capturing stack locals by
// reference into ExecuteAbiTask lambdas is unsafe on command timeout: the
// waiter returns while the job may still write those slots.
struct AbiReadI32Op {
    DS_Status status = DS_Status::DS_ERR_BAD_ARGUMENT;
    int32_t value = 0;
};

struct AbiWriteI32Op {
    DS_Status status = DS_Status::DS_ERR_BAD_ARGUMENT;
};

} // namespace

void BindCurrentScriptAbiContext(LuaScriptHostContext* hostContext) noexcept {
    t_currentAbiContext = hostContext;
}

void ClearCurrentScriptAbiContext(LuaScriptHostContext* hostContext) noexcept {
    if (t_currentAbiContext == hostContext) {
        t_currentAbiContext = nullptr;
    }
}

DS_Status AbiInstanceReadI32(ScriptHandle scriptHandle, uint32_t offset, int32_t& outValue) noexcept {
    LuaScriptHostContext* hostContext = RequireAbiContext();
    if (hostContext == nullptr || !IsValidScriptHandle(scriptHandle)) {
        return DS_Status::DS_ERR_BAD_ARGUMENT;
    }
    if (hostContext->cancellation->IsCancelled()) {
        return DS_Status::DS_ERR_CANCELLED;
    }

    const auto op = std::make_shared<AbiReadI32Op>();
    const ScriptResult command = ExecuteAbiTask(*hostContext, [hostContext, scriptHandle, offset, op]() -> DS_Status {
        uintptr_t instance = 0;
        op->status = ResolveInstanceAddress(*hostContext, scriptHandle, instance);
        if (!IsOk(op->status)) {
            return op->status;
        }

        uintptr_t address = 0;
        if (!AddOffset(instance, offset, address)) {
            op->status = DS_Status::DS_ERR_BAD_ARGUMENT;
            return op->status;
        }

        int32_t value = 0;
        if (!Engine::Memory::TryReadValue(address, value)) {
            op->status = DS_Status::DS_ERR_INVALID_POINTER;
            return op->status;
        }

        op->value = value;
        op->status = DS_Status::DS_OK;
        return op->status;
    });

    const DS_Status finalStatus = IsOk(command.status) ? op->status : command.status;
    if (IsOk(finalStatus)) {
        outValue = op->value;
    }
    RecordAbiAudit(*hostContext, ScriptAuditKind::Read, finalStatus, scriptHandle, offset,
                   static_cast<uint64_t>(static_cast<uint32_t>(IsOk(finalStatus) ? op->value : 0)));
    return finalStatus;
}

DS_Status AbiInstanceWriteI32(ScriptHandle scriptHandle, uint32_t offset, int32_t value) noexcept {
    LuaScriptHostContext* hostContext = RequireAbiContext();
    if (hostContext == nullptr || !IsValidScriptHandle(scriptHandle)) {
        return DS_Status::DS_ERR_BAD_ARGUMENT;
    }
    if (hostContext->cancellation->IsCancelled()) {
        return DS_Status::DS_ERR_CANCELLED;
    }

    const auto op = std::make_shared<AbiWriteI32Op>();
    const ScriptResult command = ExecuteAbiTask(*hostContext, [hostContext, scriptHandle, offset, value, op]() -> DS_Status {
        uintptr_t instance = 0;
        op->status = ResolveInstanceAddress(*hostContext, scriptHandle, instance);
        if (!IsOk(op->status)) {
            return op->status;
        }

        uintptr_t address = 0;
        if (!AddOffset(instance, offset, address)) {
            op->status = DS_Status::DS_ERR_BAD_ARGUMENT;
            return op->status;
        }

        if (!Engine::Memory::TryWriteValue(address, value)) {
            op->status = DS_Status::DS_ERR_INVALID_POINTER;
            return op->status;
        }

        op->status = DS_Status::DS_OK;
        return op->status;
    });

    const DS_Status finalStatus = IsOk(command.status) ? op->status : command.status;
    RecordAbiAudit(*hostContext, ScriptAuditKind::Write, finalStatus, scriptHandle, offset,
                   static_cast<uint64_t>(static_cast<uint32_t>(value)));
    return finalStatus;
}

} // namespace Scripting

DS_API uint32_t __cdecl DS_Core_GetAbiVersion() {
    return DS_ABI_VERSION;
}

DS_API uint32_t __cdecl DS_Core_GetFeatureFlags() {
    return kDsAbiFeatureFlags;
}

DS_API int32_t __cdecl DS_Core_GetBuildId(char* outBuffer, uint32_t bufferSize) {
    if (outBuffer == nullptr || bufferSize == 0) {
        return static_cast<int32_t>(Scripting::DS_Status::DS_ERR_BAD_ARGUMENT);
    }

    const size_t needed = sizeof(kDsAbiBuildId);
    if (bufferSize < needed) {
        outBuffer[0] = '\0';
        return static_cast<int32_t>(Scripting::DS_Status::DS_ERR_BUFFER_TOO_SMALL);
    }

    std::memcpy(outBuffer, kDsAbiBuildId, needed);
    return static_cast<int32_t>(Scripting::DS_Status::DS_OK);
}

DS_API int32_t __cdecl DS_Instance_ReadI32(uint64_t scriptHandle, uint32_t offset, int32_t* outValue) {
    if (outValue == nullptr) {
        return static_cast<int32_t>(Scripting::DS_Status::DS_ERR_BAD_ARGUMENT);
    }
    int32_t value = 0;
    const Scripting::DS_Status status =
        Scripting::AbiInstanceReadI32(static_cast<Scripting::ScriptHandle>(scriptHandle),
                                      offset,
                                      value);
    if (Scripting::IsOk(status)) {
        *outValue = value;
    }
    return static_cast<int32_t>(status);
}

DS_API int32_t __cdecl DS_Instance_WriteI32(uint64_t scriptHandle, uint32_t offset, int32_t value) {
    return static_cast<int32_t>(
        Scripting::AbiInstanceWriteI32(static_cast<Scripting::ScriptHandle>(scriptHandle),
                                      offset,
                                      value));
}

#endif // ENABLE_DUMPER
