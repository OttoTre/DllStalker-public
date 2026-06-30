#include "pch.h"

#ifdef ENABLE_DUMPER

extern "C" {
#include <luajit/lauxlib.h>
#include <luajit/lua.h>
}

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "scripting/bridge/lua_host_context.h"
#include "scripting/bridge/lua_proxy.h"
#include "scripting/bridge/script_bridge.h"
#include "scripting/bridge/script_reflection_api.h"
#include "scripting/runtime/script_runtime.h"

namespace Scripting
{
namespace
{
const char* FormatLuaValue(lua_State* state, int index, char* numberBuffer, size_t numberBufferSize) {
    switch (lua_type(state, index)) {
    case LUA_TNIL:
        return "nil";
    case LUA_TBOOLEAN:
        return lua_toboolean(state, index) ? "true" : "false";
    case LUA_TNUMBER:
        if (numberBuffer != nullptr && numberBufferSize > 0) {
            snprintf(numberBuffer, numberBufferSize, "%.14g", lua_tonumber(state, index));
            return numberBuffer;
        }
        return "number";
    case LUA_TSTRING:
        return lua_tolstring(state, index, nullptr);
    default:
        return lua_typename(state, lua_type(state, index));
    }
}

int LuaDsLog(lua_State* state) {
    LuaScriptHostContext* hostContext = GetHostContext(state);
    if (hostContext == nullptr) {
        return luaL_error(state, "internal script host context missing");
    }

    if (!hostContext->isUnloading &&
        hostContext->cancellation != nullptr && hostContext->cancellation->IsCancelled()) {
        return luaL_error(state, "script cancelled");
    }

    std::string line;
    const int argumentCount = lua_gettop(state);
    for (int index = 1; index <= argumentCount; ++index) {
        if (index > 1) {
            line.push_back('\t');
        }
        char numberBuffer[64]{};
        const char* formatted = FormatLuaValue(state, index, numberBuffer, sizeof(numberBuffer));
        if (formatted != nullptr) {
            line += formatted;
        }
    }

    if (hostContext->outputSink != nullptr) {
        hostContext->outputSink->Append(line);
    }

    return 0;
}

int LuaDsOnTick(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);
    luaL_checktype(state, 1, LUA_TFUNCTION);

    if (hostContext->tickCallbackRef != kScriptCallbackNoRef) {
        luaL_unref(state, LUA_REGISTRYINDEX, hostContext->tickCallbackRef);
    }
    lua_pushvalue(state, 1);
    hostContext->tickCallbackRef = luaL_ref(state, LUA_REGISTRYINDEX);
    lua_pushboolean(state, 1);
    return 1;
}

int LuaDsOnUnload(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);
    luaL_checktype(state, 1, LUA_TFUNCTION);

    if (hostContext->unloadCallbackRef != kScriptCallbackNoRef) {
        luaL_unref(state, LUA_REGISTRYINDEX, hostContext->unloadCallbackRef);
    }
    lua_pushvalue(state, 1);
    hostContext->unloadCallbackRef = luaL_ref(state, LUA_REGISTRYINDEX);
    lua_pushboolean(state, 1);
    return 1;
}

int LuaDsSleepMs(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);

    int milliseconds = static_cast<int>(luaL_checkinteger(state, 1));
    if (milliseconds < 0) {
        milliseconds = 0;
    }
    if (milliseconds > 60000) {
        milliseconds = 60000;
    }

    if (hostContext->cancellation != nullptr &&
        hostContext->cancellation->WaitForCancelOrTimeout(std::chrono::milliseconds(milliseconds))) {
        return luaL_error(state, "script cancelled");
    }
    return 0;
}

int LuaDsNowMs(lua_State* state) {
    lua_pushnumber(state, static_cast<lua_Number>(GetTickCount64()));
    return 1;
}

int LuaDsStop(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    if (hostContext->cancellation != nullptr) {
        hostContext->cancellation->RequestCancel();
    }
    lua_pushboolean(state, 1);
    return 1;
}

int LuaDsImages(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);

    const ScriptImagesResult imagesResult =
        hostContext->reflectionApi->Images(hostContext->instanceId, *hostContext->cancellation);
    if (!IsOk(imagesResult.result.status)) {
        return PushNilError(state, imagesResult.result);
    }

    lua_newtable(state);
    int luaIndex = 1;
    for (const auto& image : imagesResult.images) {
        lua_newtable(state);
        lua_pushlstring(state, image.proxy.name.data(), image.proxy.name.size());
        lua_setfield(state, -2, "name");
        lua_pushinteger(state, image.classCount);
        lua_setfield(state, -2, "class_count");
        PushProxy(state, image.proxy);
        lua_setfield(state, -2, "handle");
        lua_rawseti(state, -2, luaIndex++);
    }
    return 1;
}

int LuaDsFindImage(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);

    const char* imageName = luaL_checkstring(state, 1);
    ScriptProxyInfo proxy{};
    const ScriptApiResult result =
        hostContext->reflectionApi->FindImage(hostContext->instanceId,
                                              *hostContext->cancellation,
                                              imageName ? imageName : "",
                                              proxy);
    if (!IsOk(result.status)) {
        return PushNilError(state, result);
    }
    PushProxy(state, proxy);
    return 1;
}

int LuaDsFindClass(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);

    const char* imageName = luaL_checkstring(state, 1);
    const char* className = luaL_checkstring(state, 2);
    const char* classNamespace = luaL_optstring(state, 3, "");

    ScriptProxyInfo proxy{};
    const ScriptApiResult result =
        hostContext->reflectionApi->FindClass(hostContext->instanceId,
                                              *hostContext->cancellation,
                                              imageName ? imageName : "",
                                              className ? className : "",
                                              classNamespace ? classNamespace : "",
                                              proxy);
    if (!IsOk(result.status)) {
        return PushNilError(state, result);
    }
    PushProxy(state, proxy);
    return 1;
}

int LuaDsFindMethod(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);

    LuaProxy* classProxy = CheckProxy(state, 1, ScriptHandleKind::Class);
    const char* signatureOrName = luaL_checkstring(state, 2);
    const int argCount = lua_isnoneornil(state, 3) ? -1 : static_cast<int>(luaL_checkinteger(state, 3));

    ScriptProxyInfo proxy{};
    const ScriptApiResult result =
        hostContext->reflectionApi->FindMethod(hostContext->instanceId,
                                               *hostContext->cancellation,
                                               classProxy->handle,
                                               signatureOrName ? signatureOrName : "",
                                               argCount,
                                               proxy);
    if (!IsOk(result.status)) {
        return PushNilError(state, result);
    }
    PushProxy(state, proxy);
    return 1;
}

int LuaDsFindField(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);

    LuaProxy* classProxy = CheckProxy(state, 1, ScriptHandleKind::Class);
    const char* fieldName = luaL_checkstring(state, 2);

    ScriptProxyInfo proxy{};
    const ScriptApiResult result =
        hostContext->reflectionApi->FindField(hostContext->instanceId,
                                              *hostContext->cancellation,
                                              classProxy->handle,
                                              fieldName ? fieldName : "",
                                              proxy);
    if (!IsOk(result.status)) {
        return PushNilError(state, result);
    }
    PushProxy(state, proxy);
    return 1;
}

int LuaDsFindObjects(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);

    const char* imageName = luaL_checkstring(state, 1);
    const char* className = luaL_checkstring(state, 2);
    const char* classNamespace = luaL_optstring(state, 3, "");

    std::vector<ScriptProxyInfo> instances;
    const ScriptApiResult result =
        hostContext->reflectionApi->FindObjects(hostContext->instanceId,
                                                *hostContext->cancellation,
                                                imageName ? imageName : "",
                                                className ? className : "",
                                                classNamespace ? classNamespace : "",
                                                instances);
    if (!IsOk(result.status)) {
        return PushNilError(state, result);
    }

    lua_newtable(state);
    int luaIndex = 1;
    for (const auto& proxy : instances) {
        PushProxy(state, proxy);
        lua_rawseti(state, -2, luaIndex++);
    }
    return 1;
}

int LuaDsFindObject(lua_State* state) {
    const int resultCount = LuaDsFindObjects(state);
    if (resultCount != 1 || !lua_istable(state, -1)) {
        return resultCount;
    }

    lua_rawgeti(state, -1, 1);
    if (lua_isnil(state, -1)) {
        lua_pop(state, 2);
        lua_pushnil(state);
        lua_pushstring(state, "not found");
        return 2;
    }

    lua_remove(state, -2);
    return 1;
}

} // namespace

void RegisterDsApi(lua_State* state, LuaScriptHostContext* hostContext) {
    BindHostContext(state, hostContext);
    RegisterProxyMetatables(state);

    lua_newtable(state);
    lua_pushcfunction(state, LuaDsLog);
    lua_setfield(state, -2, "log");
    lua_pushcfunction(state, LuaDsOnTick);
    lua_setfield(state, -2, "on_tick");
    lua_pushcfunction(state, LuaDsOnUnload);
    lua_setfield(state, -2, "on_unload");
    lua_pushcfunction(state, LuaDsSleepMs);
    lua_setfield(state, -2, "sleep_ms");
    lua_pushcfunction(state, LuaDsNowMs);
    lua_setfield(state, -2, "now_ms");
    lua_pushcfunction(state, LuaDsStop);
    lua_setfield(state, -2, "stop");
    lua_pushcfunction(state, LuaDsImages);
    lua_setfield(state, -2, "images");
    lua_pushcfunction(state, LuaDsFindImage);
    lua_setfield(state, -2, "find_image");
    lua_pushcfunction(state, LuaDsFindClass);
    lua_setfield(state, -2, "find_class");
    lua_pushcfunction(state, LuaDsFindMethod);
    lua_setfield(state, -2, "find_method");
    lua_pushcfunction(state, LuaDsFindField);
    lua_setfield(state, -2, "find_field");
    lua_pushcfunction(state, LuaDsFindObject);
    lua_setfield(state, -2, "find_object");
    lua_pushcfunction(state, LuaDsFindObjects);
    lua_setfield(state, -2, "find_objects");
    lua_setglobal(state, "ds");
}

} // namespace Scripting

#endif // ENABLE_DUMPER
