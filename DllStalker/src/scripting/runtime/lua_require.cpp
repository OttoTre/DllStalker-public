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
#include "scripting/runtime/luajit_runtime_internal.h"

namespace Scripting
{
namespace
{
constexpr const char kCancellationError[] = "script cancelled";
}

std::string Utf8FromWide(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), needed, nullptr, nullptr);
    if (written <= 0) {
        return {};
    }
    if (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
}

std::filesystem::path ToPath(const std::wstring& text) {
    return std::filesystem::path(text);
}

bool IsPathInsideRoot(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    std::error_code errorCode;
    const auto rootCanonical = std::filesystem::weakly_canonical(root, errorCode);
    if (errorCode) {
        return false;
    }
    const auto candidateCanonical = std::filesystem::weakly_canonical(candidate, errorCode);
    if (errorCode) {
        return false;
    }

    auto rootString = rootCanonical.wstring();
    auto candidateString = candidateCanonical.wstring();
    if (!rootString.empty() && rootString.back() != L'\\' && rootString.back() != L'/') {
        rootString.push_back(L'\\');
    }
    if (candidateString.size() < rootString.size()) {
        return false;
    }
    return _wcsnicmp(candidateString.c_str(), rootString.c_str(), rootString.size()) == 0;
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
