#include "pch.h"

#ifdef ENABLE_DUMPER

extern "C" {
#include <luajit/lauxlib.h>
#include <luajit/lua.h>
}

#include <cstring>
#include <filesystem>
#include <string>

#include "scripting/bridge/lua_host_context.h"
#include "scripting/core/script_path_util.h"
#include "scripting/runtime/luajit_runtime_internal.h"

namespace Scripting
{
namespace
{
constexpr const char kCancellationError[] = "script cancelled";
}

bool ResolveModuleLoadPath(const LuaScriptHostContext& hostContext,
                           const char* moduleName,
                           char* outLoadPath,
                           size_t outLoadPathSize) {
    if (outLoadPath == nullptr || outLoadPathSize == 0) {
        return false;
    }
    outLoadPath[0] = '\0';

    if (moduleName == nullptr || moduleName[0] == '\0') {
        return false;
    }

    std::string normalized = moduleName;
    for (char& character : normalized) {
        if (character == '.') {
            character = '/';
        }
    }

    const std::filesystem::path packageRoot = ToPath(hostContext.packageRoot);
    std::filesystem::path candidate = packageRoot / normalized;
    if (candidate.extension().empty()) {
        candidate.replace_extension(L".lua");
    } else if (_wcsicmp(candidate.extension().c_str(), L".lua") != 0) {
        return false;
    }

    if (!IsPathInsideRoot(packageRoot, candidate)) {
        return false;
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(candidate, errorCode) || errorCode) {
        return false;
    }

    const std::string loadPath = Utf8FromWide(candidate.wstring());
    if (loadPath.empty() || loadPath.size() + 1 > outLoadPathSize) {
        return false;
    }

    memcpy(outLoadPath, loadPath.c_str(), loadPath.size() + 1);
    return true;
}

int LuaSafeRequire(lua_State* state) {
    LuaScriptHostContext* hostContext = GetHostContext(state);
    if (hostContext == nullptr) {
        return luaL_error(state, "internal script host context missing");
    }
    if (hostContext->cancellation != nullptr && hostContext->cancellation->IsCancelled()) {
        return luaL_error(state, kCancellationError);
    }

    const char* moduleName = luaL_checkstring(state, 1);

    char loadPath[4096]{};
    if (!ResolveModuleLoadPath(*hostContext, moduleName, loadPath, sizeof(loadPath))) {
        return luaL_error(state, "require blocked: %s", moduleName);
    }

    if (luaL_loadfile(state, loadPath) != 0) {
        return lua_error(state);
    }

    lua_pushvalue(state, 1);
    if (lua_pcall(state, 1, 1, 0) != 0) {
        return lua_error(state);
    }
    return 1;
}

} // namespace Scripting

#endif // ENABLE_DUMPER
