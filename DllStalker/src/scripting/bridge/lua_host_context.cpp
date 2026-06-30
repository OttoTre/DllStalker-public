#include "pch.h"

#ifdef ENABLE_DUMPER

extern "C" {
#include <luajit/lauxlib.h>
#include <luajit/lua.h>
}

#include "scripting/bridge/lua_host_context.h"
#include "scripting/bridge/script_reflection_api.h"
#include "scripting/runtime/script_runtime.h"

namespace Scripting
{
namespace
{
constexpr const char kHostContextKey[] = "DllStalker.ScriptHostContext";
}

void BindHostContext(lua_State* state, LuaScriptHostContext* hostContext) {
    lua_pushlightuserdata(state, hostContext);
    lua_setfield(state, LUA_REGISTRYINDEX, kHostContextKey);
}

LuaScriptHostContext* GetHostContext(lua_State* state) noexcept {
    lua_getfield(state, LUA_REGISTRYINDEX, kHostContextKey);
    auto* hostContext = static_cast<LuaScriptHostContext*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    return hostContext;
}

LuaScriptHostContext* RequireHostContext(lua_State* state) {
    LuaScriptHostContext* hostContext = GetHostContext(state);
    if (hostContext == nullptr || hostContext->reflectionApi == nullptr || hostContext->cancellation == nullptr) {
        luaL_error(state, "internal script host context missing");
        return nullptr;
    }
    return hostContext;
}

bool CheckCancellation(lua_State* state) {
    LuaScriptHostContext* hostContext = GetHostContext(state);
    if (hostContext != nullptr && hostContext->cancellation != nullptr &&
        hostContext->cancellation->IsCancelled()) {
        luaL_error(state, "script cancelled");
        return true;
    }
    return false;
}

} // namespace Scripting

#endif // ENABLE_DUMPER
