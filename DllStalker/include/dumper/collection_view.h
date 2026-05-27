#pragma once

#ifdef ENABLE_DUMPER

#include <vector>

#include "unity_resolver.h"
#include "types/dumper_types.h"

namespace Engine::Dumper
{
class ObjectIdentity;

// Decomposes an array (T[]) or List<T> field into one synthesized FieldInfo
// per element. The Inspector renders the result with its existing field
// row code so reference elements stay clickable for the Walker.
class CollectionView
{
public:
    CollectionView(UnityResolver& resolver, const ObjectIdentity& identity);

    std::vector<FieldInfo> GetCollectionView(const FieldInfo& field) const;

private:
    UnityResolver& m_resolver;
    const ObjectIdentity& m_identity;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
