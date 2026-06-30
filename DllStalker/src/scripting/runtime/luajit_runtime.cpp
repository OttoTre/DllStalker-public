#include "pch.h"

#ifdef ENABLE_DUMPER

extern "C" {
#include <luajit/lauxlib.h>
#include <luajit/lua.h>
}

#include <string>
#include <string_view>
#include <utility>
#include <cstdio>

#include "scripting/abi/script_abi.h"
#include "scripting/bridge/script_bridge.h"
#include "scripting/core/script_sandbox.h"
#include "scripting/runtime/luajit_runtime_internal.h"
#include "scripting/runtime/script_runtime.h"

namespace Scripting
{
namespace
{
constexpr const char kCancellationError[] = "script cancelled";

class ScopedScriptAbiContext {
public:
    explicit ScopedScriptAbiContext(LuaScriptHostContext* hostContext) noexcept
        : hostContext_(hostContext) {
        BindCurrentScriptAbiContext(hostContext_);
    }

    ~ScopedScriptAbiContext() {
        ClearCurrentScriptAbiContext(hostContext_);
    }

    ScopedScriptAbiContext(const ScopedScriptAbiContext&) = delete;
    ScopedScriptAbiContext& operator=(const ScopedScriptAbiContext&) = delete;

private:
    LuaScriptHostContext* hostContext_ = nullptr;
};

int PanicHandler(lua_State* state) {
    const char* message = lua_tolstring(state, -1, nullptr);
    if (message == nullptr) {
        message = "lua panic";
    }
    fprintf(stderr, "[Scripting] Lua panic: %s\n", message);
    return 0;
}

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
}

ScriptRuntimeResult RunScriptOnCurrentThread(const LuaScriptHostContext& hostContext) {
    lua_State* state = luaL_newstate();
    if (state == nullptr) {
        return MakeResult(ScriptRunState::Failed, DS_Status::DS_ERR_BAD_ARGUMENT, "luaL_newstate failed");
    }

    lua_atpanic(state, PanicHandler);

    LuaScriptHostContext mutableHostContext = hostContext;
    ScopedScriptAbiContext abiContext(&mutableHostContext);
    if (mutableHostContext.sandboxPolicy.profile == ScriptProfile::Safe &&
        mutableHostContext.sandboxPolicy.libraries.allowBase == false &&
        mutableHostContext.sandboxPolicy.libraries.allowTable == false) {
        mutableHostContext.sandboxPolicy = MakeDefaultSafePolicy();
    }

    const DS_Status bootstrapStatus = BootstrapSandboxedState(state, mutableHostContext.sandboxPolicy);
    if (bootstrapStatus != DS_Status::DS_OK) {
        lua_close(state);
        return MakeResult(ScriptRunState::Failed, bootstrapStatus, "sandbox bootstrap failed");
    }

    DisableJitForState(state);
    RegisterDsApi(state, &mutableHostContext);
    if (mutableHostContext.sandboxPolicy.profile == ScriptProfile::Curated) {
        RegisterCuratedApi(state, &mutableHostContext);
    }
    InstallCancellationHook(state, mutableHostContext.instructionBudget);

    const std::string entryPath = Utf8FromWide(mutableHostContext.entryFilePath);
    if (luaL_loadfile(state, entryPath.c_str()) != 0) {
        const char* errorMessage = lua_tolstring(state, -1, nullptr);
        std::string message = errorMessage ? errorMessage : "load failed";
        ClearCancellationHook(state);
        lua_close(state);
        return MakeResult(ScriptRunState::LoadError, DS_Status::DS_ERR_BAD_ARGUMENT, std::move(message));
    }

    mutableHostContext.lastTickStartedMs = GetTickCount64();
    const int callStatus = lua_pcall(state, 0, 0, 0);
    mutableHostContext.lastTickStartedMs = 0;

    ScriptRuntimeResult result{};
    if (callStatus == 0) {
        result = RunTickLoop(state, mutableHostContext);
    } else {
        const char* errorMessage = lua_tolstring(state, -1, nullptr);
        std::string message = errorMessage ? errorMessage : "runtime error";
        const bool cancellationError = IsCancellationMessage(errorMessage);
        lua_pop(state, 1);

        if (mutableHostContext.quarantineRequested) {
            result = MakeResult(ScriptRunState::Quarantined, DS_Status::DS_ERR_TIMEOUT, std::move(message));
        } else if ((mutableHostContext.cancellation != nullptr && mutableHostContext.cancellation->IsCancelled()) ||
                   cancellationError) {
            result = MakeResult(ScriptRunState::Cancelled, DS_Status::DS_ERR_CANCELLED, std::move(message));
        } else {
            result = MakeResult(ScriptRunState::Failed, DS_Status::DS_ERR_BAD_ARGUMENT, std::move(message));
        }
    }

    const ScriptRuntimeResult unloadResult = InvokeUnload(state, mutableHostContext);
    if ((unloadResult.runState == ScriptRunState::Failed ||
         unloadResult.runState == ScriptRunState::Quarantined) &&
        result.runState != ScriptRunState::Quarantined) {
        result = unloadResult;
    }
    UnrefCallback(state, mutableHostContext.tickCallbackRef);
    UnrefCallback(state, mutableHostContext.unloadCallbackRef);
    ClearCancellationHook(state);
    lua_close(state);
    return result;
}

} // namespace Scripting

#endif // ENABLE_DUMPER
