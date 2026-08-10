#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <string>
#include <vector>

#include "unity_resolver.h"
#include "types/dumper_types.h"

namespace Engine::Dumper
{
class ObjectIdentity;

// Marshals user-typed argument strings into a runtime_invoke args[] array,
// invokes through Engine::RuntimeInvoker (SEH-guarded), captures any
// managed exception, and renders the return value into a GUI-friendly
// InvokeResult.
//
// MUST be called on the Unity main thread (via Engine::Services::MainThreadDispatcher::Enqueue).
// Calling from any other thread crashes the host on most game methods.
class MethodInvoker
{
public:
    MethodInvoker(UnityResolver& resolver, const ObjectIdentity& identity);

    InvokeResult InvokeMethod(const MethodInfo& method,
                              void* instance,
                              const std::vector<std::string>& argInputs) const;

private:
    UnityResolver& m_resolver;
    const ObjectIdentity& m_identity;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
