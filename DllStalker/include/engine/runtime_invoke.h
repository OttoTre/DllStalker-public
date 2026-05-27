#pragma once

#include "pch.h"

#include "engine/unity_module.h"

// Thin wrapper around the engine's `runtime_invoke` export with an SEH
// guard. The wrapper exists because:
//   * MSVC /EHsc does NOT translate access violations into C++ exceptions,
//     and most engine asserts (cross-thread call, freed object, etc.)
//     manifest as AVs.
//   * The dumper and the main-thread dispatcher both invoke runtime_invoke
//     and both want crash recovery, so the SEH frame lives here once.
//
// Callers still need to handle managed exceptions via the outException
// parameter; SEH only covers the host-side process crash.
namespace Engine
{
class RuntimeInvoker
{
public:
    explicit RuntimeInvoker(const UnityModule& module);

    // Returns true when the call completed without an SEH fault. Sets
    // outReturn / outException either way (zeroed on SEH path so the
    // caller never reads uninitialized memory). The function pointer
    // itself is captured at call time so a re-resolve in the future is
    // picked up automatically.
    bool InvokeWithSEH(void* method, void* instance, void** args,
                       void** outException, void*& outReturn) const noexcept;

    // Direct access to the underlying export. Only the main-thread
    // dispatcher should need this (it MinHook's the address). Other
    // callers should prefer InvokeWithSEH.
    UnityExports::t_RuntimeInvoke Raw() const;

private:
    const UnityModule& m_module;
};
} // namespace Engine
