#pragma once

#include "build_config.h"

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

    // Returns true when `klass` is `targetName` or any class in its
    // inheritance chain has that name. Walks at most 12 levels.
    bool IsOrInheritsFrom(void* klass, const char* targetName) const;

private:
    const UnityResolver& m_resolver;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
