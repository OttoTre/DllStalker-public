#pragma once

#ifdef ENABLE_DUMPER

#include <string>

#include "unity_resolver.h"

namespace Engine::Dumper
{
// Cross-engine "instance -> class" helper. Hides the IL2CPP-vs-Mono header
// difference (Mono's first object word is a MonoVTable, not a MonoClass)
// and validates pointer readability before each indirection. Used by every
// part of the dumper that takes a managed instance pointer from outside
// (the Walker, Method Invoker exception decoding, Static instance scan).
class ObjectIdentity
{
public:
    explicit ObjectIdentity(const UnityResolver& resolver);

    void* KlassFromInstance(void* instance) const;

    std::string TryGetClassNameFromInstance(void* instance, void** outKlass = nullptr) const;

    // Returns the engine-reported size of the managed object pointed at
    // by `instance`, falling back to a 256-byte heuristic when the size
    // export isn't resolved.
    int GetObjectSize(void* instance) const; // Used in diagnostics for hex dumps and static field scans.

private:
    const UnityResolver& m_resolver;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
