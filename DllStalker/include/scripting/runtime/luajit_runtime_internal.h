#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstddef>
#include <cstdint>
#include <string>

#include "scripting/core/script_result.h"
#include "scripting/core/script_sandbox.h"
#include "scripting/runtime/script_runtime.h"

struct lua_State;

namespace Scripting
{

std::string Utf8FromWide(const std::wstring& text);
bool ResolveModuleLoadPath(const LuaScriptHostContext& hostContext,
                           const char* moduleName,
                           char* outLoadPath,
                           size_t outLoadPathSize);
int LuaSafeRequire(lua_State* state);

DS_Status BootstrapSandboxedState(lua_State* state, const SandboxPolicy& policy);
void DisableJitForState(lua_State* state);

void InstallCancellationHook(lua_State* state, uint32_t instructionBudget);
void ClearCancellationHook(lua_State* state);
void UnrefCallback(lua_State* state, int& callbackRef);
ScriptRuntimeResult InvokeUnload(lua_State* state, LuaScriptHostContext& hostContext);
ScriptRuntimeResult RunTickLoop(lua_State* state, LuaScriptHostContext& hostContext);

} // namespace Scripting

#endif // ENABLE_DUMPER
