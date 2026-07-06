#pragma once

#include <windows.h>

#include "engine/unity_exports.h"

// Owns Unity engine state: which runtime is loaded (IL2CPP vs Mono), the
// engine module handle, the active managed domain, and the function-pointer
// table that the rest of the engine layer reaches through.
//
// One UnityModule is constructed per process; UnityResolver composes it.
// Sub-services (ImageEnumerator, Reflection, RuntimeInvoker) take a
// reference into this object instead of duplicating module / export state.
namespace Engine
{
class UnityModule
{
public:
    bool    isIL2CPP = false;
    HMODULE hModule  = nullptr;
    void*   domain   = nullptr;

    UnityExports exports{};

    // Block until either GameAssembly.dll (IL2CPP) or mono-2.0-bdwgc.dll
    // (Mono) is loaded into the host process. Returns false on timeout.
    bool WaitForModule();

    // Resolves every export in the table once `hModule` is set. Returns
    // false when a release-required export is missing (the dumper-only
    // exports are resolved best-effort and won't fail this call).
    bool ResolveExports();

    // Polls fnGetDomain() until it returns a non-null pointer or attempts
    // run out. Wraps the call in SEH because some Mono versions trap
    // briefly on race-y first calls.
    bool ResolveDomain();

    // Idempotent. Attaches the calling thread to the active domain so the
    // engine's TLS hooks see a sane state on cross-thread reflection calls.
    void EnsureThreadAttached() const;
};
} // namespace Engine
