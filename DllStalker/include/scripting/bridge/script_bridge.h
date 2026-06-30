#pragma once

#ifdef ENABLE_DUMPER

#include "scripting/bridge/lua_host_context.h"

struct lua_State;

namespace Scripting
{
struct LuaScriptHostContext;

// Raw Lua C API registration for the ds.* and Curated surfaces.
void RegisterDsApi(lua_State* state, LuaScriptHostContext* hostContext);
void RegisterCuratedApi(lua_State* state, LuaScriptHostContext* hostContext);

} // namespace Scripting

#endif // ENABLE_DUMPER
