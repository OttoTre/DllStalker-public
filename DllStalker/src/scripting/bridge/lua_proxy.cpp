#include "pch.h"

#ifdef ENABLE_DUMPER

extern "C" {
#include <luajit/lauxlib.h>
#include <luajit/lua.h>
}

#include <cstdio>
#include <cmath>
#include <limits>
#include <vector>

#include "scripting/abi/script_abi.h"
#include "scripting/bridge/lua_host_context.h"
#include "scripting/bridge/lua_proxy.h"
#include "scripting/bridge/script_reflection_api.h"
#include "scripting/runtime/script_runtime.h"

namespace Scripting
{
constexpr const char kImageMetatable[] = "DllStalker.ds.Image";
constexpr const char kClassMetatable[] = "DllStalker.ds.Class";
constexpr const char kMethodMetatable[] = "DllStalker.ds.Method";
constexpr const char kFieldMetatable[] = "DllStalker.ds.Field";
constexpr const char kInstanceMetatable[] = "DllStalker.ds.Instance";

const char* MetatableForKind(ScriptHandleKind kind) noexcept {
    switch (kind) {
    case ScriptHandleKind::Image: return kImageMetatable;
    case ScriptHandleKind::Class: return kClassMetatable;
    case ScriptHandleKind::Method: return kMethodMetatable;
    case ScriptHandleKind::Field: return kFieldMetatable;
    case ScriptHandleKind::Instance: return kInstanceMetatable;
    default: return nullptr;
    }
}

const char* KindName(ScriptHandleKind kind) noexcept {
    switch (kind) {
    case ScriptHandleKind::Image: return "Image";
    case ScriptHandleKind::Class: return "Class";
    case ScriptHandleKind::Method: return "Method";
    case ScriptHandleKind::Field: return "Field";
    case ScriptHandleKind::Instance: return "Instance";
    default: return "Invalid";
    }
}

void FormatAddressHex(uintptr_t address, char* buffer, size_t bufferSize) {
    snprintf(buffer, bufferSize, "0x%016llX", static_cast<unsigned long long>(address));
}

DS_Status ResolveProxyAddress(LuaScriptHostContext& hostContext,
                              const LuaProxy& proxy,
                              uintptr_t& outAddress) {
    if (hostContext.handleRegistry == nullptr) {
        return DS_Status::DS_ERR_BAD_ARGUMENT;
    }

    const ResolvedHandle resolved =
        hostContext.handleRegistry->ResolveAndValidate(proxy.handle, hostContext.instanceId, proxy.kind);
    if (!IsOk(resolved.status)) {
        return resolved.status;
    }

    outAddress = resolved.transientNativeAddress;
    return DS_Status::DS_OK;
}

int PushNilError(lua_State* state, const ScriptApiResult& result) {
    lua_pushnil(state);
    if (!result.message.empty()) {
        lua_pushstring(state, result.message.c_str());
    } else {
        lua_pushstring(state, StatusToString(result.status));
    }
    return 2;
}

int PushNilStatus(lua_State* state, DS_Status status) {
    lua_pushnil(state);
    lua_pushstring(state, StatusToString(status));
    return 2;
}

void PushProxy(lua_State* state, const ScriptProxyInfo& proxy) {
    auto* userdata = static_cast<LuaProxy*>(lua_newuserdata(state, sizeof(LuaProxy)));
    userdata->handle = proxy.handle;
    userdata->kind = proxy.kind;

    const char* metatable = MetatableForKind(proxy.kind);
    if (metatable != nullptr) {
        luaL_getmetatable(state, metatable);
        lua_setmetatable(state, -2);
    }
}

LuaProxy* CheckProxy(lua_State* state, int index, ScriptHandleKind expectedKind) {
    const char* metatable = MetatableForKind(expectedKind);
    auto* proxy = static_cast<LuaProxy*>(luaL_checkudata(state, index, metatable));
    if (proxy == nullptr || proxy->kind != expectedKind || !IsValidScriptHandle(proxy->handle)) {
        luaL_error(state, "invalid %s proxy", KindName(expectedKind));
        return nullptr;
    }
    return proxy;
}

LuaProxy* TestInstanceProxy(lua_State* state, int index) {
    void* userdata = lua_touserdata(state, index);
    if (userdata == nullptr) {
        return nullptr;
    }
    if (!lua_getmetatable(state, index)) {
        return nullptr;
    }
    luaL_getmetatable(state, kInstanceMetatable);
    const bool matches = lua_rawequal(state, -1, -2) != 0;
    lua_pop(state, 2);
    if (!matches) {
        return nullptr;
    }
    return static_cast<LuaProxy*>(userdata);
}

LuaCuratedInstance* CheckCuratedInstance(lua_State* state, int index) {
    return static_cast<LuaCuratedInstance*>(
        luaL_checkudata(state, index, "DllStalker.ds.types.Instance"));
}

LuaCuratedInstance* TestCuratedInstance(lua_State* state, int index) {
    void* userdata = lua_touserdata(state, index);
    if (userdata == nullptr) {
        return nullptr;
    }
    if (!lua_getmetatable(state, index)) {
        return nullptr;
    }
    luaL_getmetatable(state, "DllStalker.ds.types.Instance");
    const bool matches = lua_rawequal(state, -1, -2) != 0;
    lua_pop(state, 2);
    if (!matches) {
        return nullptr;
    }
    return static_cast<LuaCuratedInstance*>(userdata);
}

ScriptHandle ReadInstanceHandle(lua_State* state, int index) {
    if (auto* instance = TestCuratedInstance(state, index)) {
        return instance->handle;
    }
    LuaProxy* proxy = CheckProxy(state, index, ScriptHandleKind::Instance);
    return proxy != nullptr ? proxy->handle : kInvalidScriptHandle;
}

void PushValue(lua_State* state, const ScriptValue& value) {
    switch (value.kind) {
    case ScriptValueKind::Invalid:
        lua_pushnil(state);
        break;
    case ScriptValueKind::Nil:
        lua_pushnil(state);
        break;
    case ScriptValueKind::Boolean:
        lua_pushboolean(state, value.booleanValue ? 1 : 0);
        break;
    case ScriptValueKind::Integer:
        // Prefer integer when it fits lua_Integer; else string for full fidelity.
        if (value.integerValue >= static_cast<int64_t>((std::numeric_limits<lua_Integer>::min)())
            && value.integerValue <= static_cast<int64_t>((std::numeric_limits<lua_Integer>::max)())) {
            lua_pushinteger(state, static_cast<lua_Integer>(value.integerValue));
        }
        else {
            const std::string text = std::to_string(value.integerValue);
            lua_pushlstring(state, text.data(), text.size());
        }
        break;
    case ScriptValueKind::Unsigned:
        if (value.unsignedValue <= static_cast<uint64_t>((std::numeric_limits<lua_Integer>::max)())) {
            lua_pushinteger(state, static_cast<lua_Integer>(value.unsignedValue));
        }
        else {
            const std::string text = std::to_string(value.unsignedValue);
            lua_pushlstring(state, text.data(), text.size());
        }
        break;
    case ScriptValueKind::Number:
        lua_pushnumber(state, static_cast<lua_Number>(value.numberValue));
        break;
    case ScriptValueKind::String:
        lua_pushlstring(state, value.stringValue.data(), value.stringValue.size());
        break;
    case ScriptValueKind::Handle: {
        ScriptProxyInfo proxy{};
        proxy.handle = value.handleValue;
        proxy.kind = ScriptHandleKind::Instance;
        PushProxy(state, proxy);
        break;
    }
    default:
        lua_pushnil(state);
        break;
    }
}

ScriptValue ReadLuaValue(lua_State* state, int index) {
    ScriptValue value{};
    const int type = lua_type(state, index);
    switch (type) {
    case LUA_TNIL:
        value.kind = ScriptValueKind::Nil;
        break;
    case LUA_TBOOLEAN:
        value.kind = ScriptValueKind::Boolean;
        value.booleanValue = lua_toboolean(state, index) != 0;
        break;
    case LUA_TNUMBER: {
        const lua_Number number = lua_tonumber(state, index);
        double intPart = 0.0;
        if (std::modf(static_cast<double>(number), &intPart) == 0.0) {
            value.kind = ScriptValueKind::Integer;
            value.integerValue = static_cast<int64_t>(intPart);
        } else {
            value.kind = ScriptValueKind::Number;
            value.numberValue = static_cast<double>(number);
        }
        break;
    }
    case LUA_TSTRING: {
        size_t length = 0;
        const char* text = lua_tolstring(state, index, &length);
        value.kind = ScriptValueKind::String;
        value.stringValue.assign(text != nullptr ? text : "", length);
        break;
    }
    case LUA_TUSERDATA: {
        auto* proxy = TestInstanceProxy(state, index);
        if (proxy != nullptr && proxy->kind == ScriptHandleKind::Instance) {
            value.kind = ScriptValueKind::Handle;
            value.handleValue = proxy->handle;
            break;
        }
        auto* typedInstance = TestCuratedInstance(state, index);
        if (typedInstance != nullptr && IsValidScriptHandle(typedInstance->handle)) {
            value.kind = ScriptValueKind::Handle;
            value.handleValue = typedInstance->handle;
            break;
        }
        value.kind = ScriptValueKind::Invalid;
        break;
    }
    default:
        value.kind = ScriptValueKind::Invalid;
        break;
    }
    return value;
}

int LuaProxyToString(lua_State* state) {
    auto* proxy = static_cast<LuaProxy*>(lua_touserdata(state, 1));
    if (proxy == nullptr) {
        lua_pushstring(state, "ds.Invalid");
        return 1;
    }

    char buffer[64]{};
    snprintf(buffer, sizeof(buffer), "ds.%s<handle>", KindName(proxy->kind));
    lua_pushstring(state, buffer);
    return 1;
}

int LuaProxyAddressHex(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);

    auto* proxy = static_cast<LuaProxy*>(lua_touserdata(state, 1));
    if (proxy == nullptr || !IsValidScriptHandle(proxy->handle)) {
        return luaL_error(state, "invalid proxy");
    }

    uintptr_t address = 0;
    const DS_Status status = ResolveProxyAddress(*hostContext, *proxy, address);
    if (!IsOk(status)) {
        return PushNilStatus(state, status);
    }

    char buffer[32]{};
    FormatAddressHex(address, buffer, sizeof(buffer));
    lua_pushstring(state, buffer);
    return 1;
}

int LuaProxyGetRawAddress(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);
    if (hostContext->sandboxPolicy.profile != ScriptProfile::Curated) {
        return luaL_error(state, "get_raw_address is Curated-only");
    }

    auto* proxy = static_cast<LuaProxy*>(lua_touserdata(state, 1));
    if (proxy == nullptr || !IsValidScriptHandle(proxy->handle)) {
        return luaL_error(state, "invalid proxy");
    }

    uintptr_t address = 0;
    const DS_Status status = ResolveProxyAddress(*hostContext, *proxy, address);
    if (!IsOk(status)) {
        return PushNilStatus(state, status);
    }

    lua_pushlightuserdata(state, reinterpret_cast<void*>(address));
    return 1;
}

int LuaInstanceGet(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);

    LuaProxy* instance = CheckProxy(state, 1, ScriptHandleKind::Instance);
    const char* fieldName = luaL_checkstring(state, 2);
    const ScriptFieldValueResult result =
        hostContext->reflectionApi->GetField(hostContext->instanceId,
                                             *hostContext->cancellation,
                                             instance->handle,
                                             fieldName ? fieldName : "");
    if (!IsOk(result.result.status)) {
        return PushNilError(state, result.result);
    }
    PushValue(state, result.value);
    return 1;
}

int LuaInstanceSet(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);

    LuaProxy* instance = CheckProxy(state, 1, ScriptHandleKind::Instance);
    const char* fieldName = luaL_checkstring(state, 2);
    ScriptValue value = ReadLuaValue(state, 3);
    const ScriptApiResult result =
        hostContext->reflectionApi->SetField(hostContext->instanceId,
                                             *hostContext->cancellation,
                                             instance->handle,
                                             fieldName ? fieldName : "",
                                             value);
    if (!IsOk(result.status)) {
        return PushNilError(state, result);
    }
    lua_pushboolean(state, 1);
    return 1;
}

int LuaInstanceInvoke(lua_State* state) {
    LuaScriptHostContext* hostContext = RequireHostContext(state);
    CheckCancellation(state);

    LuaProxy* instance = CheckProxy(state, 1, ScriptHandleKind::Instance);
    const char* signature = luaL_checkstring(state, 2);
    const int argumentCount = lua_gettop(state) - 2;
    std::vector<ScriptValue> args;
    args.reserve(argumentCount > 0 ? static_cast<size_t>(argumentCount) : 0);
    for (int index = 0; index < argumentCount; ++index) {
        args.push_back(ReadLuaValue(state, index + 3));
    }

    const ScriptInvokeResult result =
        hostContext->reflectionApi->Invoke(hostContext->instanceId,
                                           *hostContext->cancellation,
                                           instance->handle,
                                           signature ? signature : "",
                                           args);
    if (!IsOk(result.result.status)) {
        return PushNilError(state, result.result);
    }
    PushValue(state, result.value);
    return 1;
}

void CreateMetatable(lua_State* state,
                     const char* name,
                     const luaL_Reg* methods,
                     bool indexSelf) {
    if (luaL_newmetatable(state, name)) {
        lua_pushcfunction(state, LuaProxyToString);
        lua_setfield(state, -2, "__tostring");

        if (methods != nullptr) {
            luaL_setfuncs(state, methods, 0);
            if (indexSelf) {
                lua_pushvalue(state, -1);
                lua_setfield(state, -2, "__index");
            }
        }
    }
    lua_pop(state, 1);
}

void RegisterProxyMetatables(lua_State* state) {
    LuaScriptHostContext* hostContext = GetHostContext(state);
    const bool allowRawAddress =
        hostContext != nullptr && hostContext->sandboxPolicy.profile == ScriptProfile::Curated;

    static const luaL_Reg commonMethods[] = {
        {"address_hex", LuaProxyAddressHex},
        {nullptr, nullptr},
    };
    static const luaL_Reg commonCuratedMethods[] = {
        {"address_hex", LuaProxyAddressHex},
        {"get_raw_address", LuaProxyGetRawAddress},
        {nullptr, nullptr},
    };
    static const luaL_Reg instanceMethods[] = {
        {"get", LuaInstanceGet},
        {"set", LuaInstanceSet},
        {"invoke", LuaInstanceInvoke},
        {"address_hex", LuaProxyAddressHex},
        {nullptr, nullptr},
    };
    static const luaL_Reg instanceCuratedMethods[] = {
        {"get", LuaInstanceGet},
        {"set", LuaInstanceSet},
        {"invoke", LuaInstanceInvoke},
        {"address_hex", LuaProxyAddressHex},
        {"get_raw_address", LuaProxyGetRawAddress},
        {nullptr, nullptr},
    };

    const luaL_Reg* nonInstanceMethods = allowRawAddress ? commonCuratedMethods : commonMethods;
    CreateMetatable(state, kImageMetatable, nonInstanceMethods, true);
    CreateMetatable(state, kClassMetatable, nonInstanceMethods, true);
    CreateMetatable(state, kMethodMetatable, nonInstanceMethods, true);
    CreateMetatable(state, kFieldMetatable, nonInstanceMethods, true);
    CreateMetatable(state,
                    kInstanceMetatable,
                    allowRawAddress ? instanceCuratedMethods : instanceMethods,
                    true);
}

} // namespace Scripting

#endif // ENABLE_DUMPER
