#include "pch.h"

#ifdef ENABLE_DUMPER

extern "C" {
#include <luajit/lauxlib.h>
#include <luajit/lua.h>
#include <luajit/lualib.h>
#include <luajit/luajit.h>
}

#include "scripting/runtime/luajit_runtime_internal.h"

namespace Scripting
{
namespace
{
bool OpenWhitelistedLibrary(lua_State* state, lua_CFunction opener) {
    lua_pushcfunction(state, opener);
    if (lua_pcall(state, 0, 0, 0) != 0) {
        lua_pop(state, 1);
        return false;
    }
    return true;
}

void ApplyBlockedBaseGlobals(lua_State* state) {
    const size_t count = GetBlockedBaseGlobalCount();
    const char* const* names = GetBlockedBaseGlobals();
    for (size_t index = 0; index < count; ++index) {
        lua_pushnil(state);
        lua_setglobal(state, names[index]);
    }
}
} // namespace

void DisableJitForState(lua_State* state) {
    luaJIT_setmode(state, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
}

DS_Status BootstrapSandboxedState(lua_State* state, const SandboxPolicy& policy) {
    if (state == nullptr) {
        return DS_Status::DS_ERR_BAD_ARGUMENT;
    }

    if (policy.profile != ScriptProfile::Safe && policy.profile != ScriptProfile::Curated) {
        return DS_Status::DS_ERR_BAD_ARGUMENT;
    }

    if (policy.libraries.allowBase) {
        if (!OpenWhitelistedLibrary(state, luaopen_base)) {
            return DS_Status::DS_ERR_INTERNAL;
        }
        ApplyBlockedBaseGlobals(state);
    }
    if (policy.libraries.allowTable) {
        if (!OpenWhitelistedLibrary(state, luaopen_table)) {
            return DS_Status::DS_ERR_INTERNAL;
        }
    }
    if (policy.libraries.allowString) {
        if (!OpenWhitelistedLibrary(state, luaopen_string)) {
            return DS_Status::DS_ERR_INTERNAL;
        }
    }
    if (policy.libraries.allowMath) {
        if (!OpenWhitelistedLibrary(state, luaopen_math)) {
            return DS_Status::DS_ERR_INTERNAL;
        }
    }

    if (policy.gates.allowWhitelistedRequireOnly) {
        lua_pushcfunction(state, LuaSafeRequire);
        lua_setglobal(state, "require");
    }

    lua_pushnil(state);
    lua_setglobal(state, "dofile");
    lua_pushnil(state);
    lua_setglobal(state, "loadfile");
    lua_pushnil(state);
    lua_setglobal(state, "load");
    lua_pushnil(state);
    lua_setglobal(state, "loadstring");
    lua_pushnil(state);
    lua_setglobal(state, "package");
    lua_pushnil(state);
    lua_setglobal(state, "io");
    lua_pushnil(state);
    lua_setglobal(state, "os");
    lua_pushnil(state);
    lua_setglobal(state, "debug");
    lua_pushnil(state);
    lua_setglobal(state, "jit");
    lua_pushnil(state);
    lua_setglobal(state, "ffi");

    return DS_Status::DS_OK;
}

} // namespace Scripting

#endif // ENABLE_DUMPER
