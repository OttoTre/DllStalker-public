#include "pch.h"

#include "unity_resolver.h"

#include "services/bootstrap_log.h"

#include <cstring>

namespace Engine
{
UnityResolver::UnityResolver()
    : module()
    , images(module)
    , reflection(module)
    , invoker(module)
{
}

UnityResolver& UnityResolver::Instance() {
    static UnityResolver instance;
    return instance;
}

UnityResolver& Unity = UnityResolver::Instance();

bool UnityResolver::Init() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    if (strstr(path, "CrashHandler")) return false;

    Engine::Services::BootstrapLog::Write("[*] Unity Resolver Initializing...\n");

    if (!module.WaitForModule()) return false;
    if (!module.ResolveExports()) return false;

    Engine::Services::BootstrapLog::Write("[*] Exports resolved. Attempting to get domain...\n");
    if (!module.ResolveDomain()) return false;

    module.EnsureThreadAttached();
    Engine::Services::BootstrapLog::Write(
        "[+] Unity Resolver Ready (%s)\n", module.isIL2CPP ? "IL2CPP" : "Mono");
    return true;
}
} // namespace Engine
