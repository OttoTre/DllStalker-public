#include "pch.h"

#ifdef ENABLE_DUMPER

extern "C" {
#include <luajit/lauxlib.h>
#include <luajit/lua.h>
}

#include "scripting/abi/script_abi.h"
#include "scripting/bridge/lua_host_context.h"
#include "scripting/bridge/lua_proxy.h"
#include "scripting/bridge/script_bridge.h"
#include "scripting/runtime/script_runtime.h"

namespace Scripting
{
namespace
{
int LuaCuratedInstanceToString(lua_State* state) {
    auto* instance = static_cast<LuaCuratedInstance*>(lua_touserdata(state, 1));
    if (instance == nullptr || !IsValidScriptHandle(instance->handle)) {
        lua_pushstring(state, "ds.types.Instance<invalid>");
        return 1;
    }
    lua_pushstring(state, "ds.types.Instance<handle>");
    return 1;
}

int LuaCuratedInstanceReadI32(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);
    if (hostContext->sandboxPolicy.profile != ScriptProfile::Curated) {
        return luaL_error(state, "Curated instance helpers require Curated profile");
    }

    LuaCuratedInstance* instance = CheckCuratedInstance(state, 1);
    const uint32_t offset = static_cast<uint32_t>(luaL_checkinteger(state, 2));

    int32_t value = 0;
    const DS_Status status = AbiInstanceReadI32(instance->handle, offset, value);
    if (!IsOk(status)) {
        return PushNilStatus(state, status);
    }

    lua_pushinteger(state, static_cast<lua_Integer>(value));
    return 1;
}

int LuaCuratedInstanceWriteI32(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);
    if (hostContext->sandboxPolicy.profile != ScriptProfile::Curated) {
        return luaL_error(state, "Curated instance helpers require Curated profile");
    }

    LuaCuratedInstance* instance = CheckCuratedInstance(state, 1);
    const uint32_t offset = static_cast<uint32_t>(luaL_checkinteger(state, 2));
    const int32_t value = static_cast<int32_t>(luaL_checkinteger(state, 3));

    const DS_Status status = AbiInstanceWriteI32(instance->handle, offset, value);
    if (!IsOk(status)) {
        return PushNilStatus(state, status);
    }

    lua_pushboolean(state, 1);
    return 1;
}

int LuaCuratedInstanceWrap(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);
    if (hostContext->sandboxPolicy.profile != ScriptProfile::Curated) {
        return luaL_error(state, "ds.types.Instance.wrap requires Curated profile");
    }

    LuaProxy* proxy = CheckProxy(state, 1, ScriptHandleKind::Instance);
    auto* instance = static_cast<LuaCuratedInstance*>(
        lua_newuserdata(state, sizeof(LuaCuratedInstance)));
    instance->handle = proxy->handle;
    luaL_getmetatable(state, "DllStalker.ds.types.Instance");
    lua_setmetatable(state, -2);
    return 1;
}

int LuaDsAbiVersion(lua_State* state) {
    lua_pushinteger(state, static_cast<lua_Integer>(DS_Core_GetAbiVersion()));
    return 1;
}

int LuaDsAbiFeatureFlags(lua_State* state) {
    lua_pushinteger(state, static_cast<lua_Integer>(DS_Core_GetFeatureFlags()));
    return 1;
}

int LuaDsAbiStatusToError(lua_State* state) {
    const DS_Status status = static_cast<DS_Status>(luaL_checkinteger(state, 1));
    lua_pushstring(state, StatusToString(status));
    return 1;
}

int LuaDsAbiInstanceReadI32(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);
    if (hostContext->sandboxPolicy.profile != ScriptProfile::Curated) {
        return luaL_error(state, "ds_abi requires Curated profile");
    }

    const ScriptHandle handle = ReadInstanceHandle(state, 1);
    const uint32_t offset = static_cast<uint32_t>(luaL_checkinteger(state, 2));

    int32_t value = 0;
    const DS_Status status = AbiInstanceReadI32(handle, offset, value);
    if (!IsOk(status)) {
        lua_pushnil(state);
        lua_pushinteger(state, static_cast<lua_Integer>(status));
        lua_pushstring(state, StatusToString(status));
        return 3;
    }

    lua_pushinteger(state, static_cast<lua_Integer>(value));
    return 1;
}

int LuaDsAbiInstanceWriteI32(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);
    if (hostContext->sandboxPolicy.profile != ScriptProfile::Curated) {
        return luaL_error(state, "ds_abi requires Curated profile");
    }

    const ScriptHandle handle = ReadInstanceHandle(state, 1);
    const uint32_t offset = static_cast<uint32_t>(luaL_checkinteger(state, 2));
    const int32_t value = static_cast<int32_t>(luaL_checkinteger(state, 3));

    const DS_Status status = AbiInstanceWriteI32(handle, offset, value);
    if (!IsOk(status)) {
        lua_pushboolean(state, 0);
        lua_pushinteger(state, static_cast<lua_Integer>(status));
        lua_pushstring(state, StatusToString(status));
        return 3;
    }

    lua_pushboolean(state, 1);
    return 1;
}

void RegisterCuratedInstanceType(lua_State* state) {
    static const luaL_Reg methods[] = {
        {"read_i32", LuaCuratedInstanceReadI32},
        {"write_i32", LuaCuratedInstanceWriteI32},
        {nullptr, nullptr},
    };

    if (luaL_newmetatable(state, "DllStalker.ds.types.Instance")) {
        lua_pushcfunction(state, LuaCuratedInstanceToString);
        lua_setfield(state, -2, "__tostring");
        luaL_setfuncs(state, methods, 0);
        lua_pushvalue(state, -1);
        lua_setfield(state, -2, "__index");
    }
    lua_pop(state, 1);
}
} // namespace

void RegisterCuratedApi(lua_State* state, LuaScriptHostContext* hostContext) {
    if (state == nullptr || hostContext == nullptr || hostContext->sandboxPolicy.profile != ScriptProfile::Curated) {
        return;
    }

    RegisterCuratedInstanceType(state);

    lua_newtable(state);
    lua_pushinteger(state, static_cast<lua_Integer>(DS_Status::DS_OK));
    lua_setfield(state, -2, "OK");
    lua_pushinteger(state, static_cast<lua_Integer>(DS_ABI_VERSION));
    lua_setfield(state, -2, "ABI_VERSION");
    lua_pushinteger(state, static_cast<lua_Integer>(DS_ABI_FEATURE_INSTANCE_PRIMITIVES));
    lua_setfield(state, -2, "FEATURE_INSTANCE_PRIMITIVES");
    lua_pushinteger(state, static_cast<lua_Integer>(DS_ABI_FEATURE_SINGLE_PROXY_DLL));
    lua_setfield(state, -2, "FEATURE_SINGLE_PROXY_DLL");
    lua_pushcfunction(state, LuaDsAbiVersion);
    lua_setfield(state, -2, "abi_version");
    lua_pushcfunction(state, LuaDsAbiFeatureFlags);
    lua_setfield(state, -2, "feature_flags");
    lua_pushcfunction(state, LuaDsAbiStatusToError);
    lua_setfield(state, -2, "status_to_error");
    lua_pushcfunction(state, LuaDsAbiInstanceReadI32);
    lua_setfield(state, -2, "instance_read_i32");
    lua_pushcfunction(state, LuaDsAbiInstanceWriteI32);
    lua_setfield(state, -2, "instance_write_i32");
    lua_setglobal(state, "ds_abi");

    lua_getglobal(state, "ds");
    if (!lua_istable(state, -1)) {
        lua_pop(state, 1);
        return;
    }

    lua_newtable(state);
    lua_newtable(state);
    lua_pushcfunction(state, LuaCuratedInstanceWrap);
    lua_setfield(state, -2, "wrap");
    lua_setfield(state, -2, "Instance");
    lua_setfield(state, -2, "types");
    lua_pop(state, 1);
}

} // namespace Scripting

#endif // ENABLE_DUMPER
