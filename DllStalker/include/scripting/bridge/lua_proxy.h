#pragma once

#ifdef ENABLE_DUMPER

#include "scripting/bridge/script_value.h"

struct lua_State;

namespace Scripting
{
struct LuaProxy {
    ScriptHandle handle = kInvalidScriptHandle;
    ScriptHandleKind kind = ScriptHandleKind::Invalid;
};

struct LuaCuratedInstance {
    ScriptHandle handle = kInvalidScriptHandle;
};

int PushNilError(lua_State* state, const ScriptApiResult& result);
int PushNilStatus(lua_State* state, DS_Status status);
void PushProxy(lua_State* state, const ScriptProxyInfo& proxy);
void PushValue(lua_State* state, const ScriptValue& value);
ScriptValue ReadLuaValue(lua_State* state, int index);
LuaProxy* CheckProxy(lua_State* state, int index, ScriptHandleKind expectedKind);
LuaProxy* TestInstanceProxy(lua_State* state, int index);
LuaCuratedInstance* CheckCuratedInstance(lua_State* state, int index);
LuaCuratedInstance* TestCuratedInstance(lua_State* state, int index);
ScriptHandle ReadInstanceHandle(lua_State* state, int index);
void RegisterProxyMetatables(lua_State* state);

} // namespace Scripting

#endif // ENABLE_DUMPER
