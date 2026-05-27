#pragma once

#ifdef ENABLE_DUMPER

#include <vector>

#include "unity_resolver.h"

namespace Engine::Dumper
{
class FieldCatalog;

// Cheap, side-effect-free instance discovery: walks the static fields of
// a class and treats any pointer-shaped slot whose first word matches
// `klass` as a candidate instance. Used as the fast path in the Inspector
// before falling back to the live FindObjectsOfType API.
class StaticInstanceFinder
{
public:
    StaticInstanceFinder(UnityResolver& resolver, FieldCatalog& fields);

    // First candidate, or nullptr if none.
    void* FindStaticInstance(void* klass);

    // All deduplicated candidates (caller picks).
    std::vector<void*> FindStaticInstanceCandidates(void* klass);

private:
    UnityResolver& m_resolver;
    FieldCatalog&  m_fields;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
