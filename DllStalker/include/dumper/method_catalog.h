#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <vector>

#include "unity_resolver.h"
#include "types/dumper_types.h"

namespace Engine::Dumper
{
// Reads the method list off of a class descriptor and resolves each
// method's name, address, parameter count, and parameter type names.
// Hides the IL2CPP-vs-Mono difference in how parameter signatures are
// surfaced (IL2CPP exposes method_get_param_count directly; Mono needs
// a signature handle and signature_get_params).
class MethodCatalog
{
public:
    explicit MethodCatalog(UnityResolver& resolver);

    std::vector<MethodInfo> GetRawMethods(void* klass);

private:
    UnityResolver& m_resolver;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
