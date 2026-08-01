#include "pch.h"

#include "services/module_path.h"

namespace Engine::Services
{
namespace
{
HMODULE GetSelfModuleHandle() noexcept {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&GetSelfModuleHandle),
                       &module);
    return module;
}
} // namespace

std::wstring GetProxyDllDirectory() {
    const HMODULE module = GetSelfModuleHandle();
    if (module == nullptr) {
        return L".";
    }

    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L".";
    }

    std::wstring directory(path, length);
    const size_t slash = directory.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        directory.resize(slash);
    }
    return directory;
}
} // namespace Engine::Services
