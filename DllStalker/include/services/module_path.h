#pragma once

#include <string>

namespace Engine::Services
{
// Directory containing this proxy DLL (version.dll / DllStalker). Returns L"." on failure.
std::wstring GetProxyDllDirectory();
} // namespace Engine::Services
