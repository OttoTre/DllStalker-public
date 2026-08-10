#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <vector>

#include "unity_resolver.h"

namespace Engine::Dumper
{
class FieldCatalog;
class ObjectIdentity;

// Cheap, side-effect-free instance discovery: walks the static fields of
// a class and treats any pointer-shaped slot whose object class matches
// `klass` (via ObjectIdentity::KlassFromInstance) as a candidate. Used as
// the fast path in the Inspector before falling back to the live
// FindObjectsOfType API.
class StaticInstanceFinder
{
public:
    StaticInstanceFinder(UnityResolver& resolver, FieldCatalog& fields, ObjectIdentity& identity);

    // All deduplicated candidates (caller picks).
    std::vector<void*> FindStaticInstanceCandidates(void* klass);

private:
    UnityResolver&  m_resolver;
    FieldCatalog&   m_fields;
    ObjectIdentity& m_identity;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
