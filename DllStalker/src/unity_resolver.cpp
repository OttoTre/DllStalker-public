#include "pch.h"

#include "unity_resolver.h"

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

    printf("[*] Unity Resolver Initializing...\n");

    if (!module.WaitForModule()) return false;
    if (!module.ResolveExports()) return false;

    printf("[*] Exports resolved. Attempting to get domain...\n");
    if (!module.ResolveDomain()) return false;

    module.EnsureThreadAttached();
    printf("[+] Unity Resolver Ready (%s)\n", module.isIL2CPP ? "IL2CPP" : "Mono");
    return true;
}
} // namespace Engine
