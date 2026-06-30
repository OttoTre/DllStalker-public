#pragma once

#ifdef ENABLE_DUMPER

struct lua_State;

namespace Scripting
{
struct LuaScriptHostContext;

void BindHostContext(lua_State* state, LuaScriptHostContext* hostContext);
LuaScriptHostContext* GetHostContext(lua_State* state) noexcept;
LuaScriptHostContext* RequireHostContext(lua_State* state);
bool CheckCancellation(lua_State* state);

} // namespace Scripting

#endif // ENABLE_DUMPER
