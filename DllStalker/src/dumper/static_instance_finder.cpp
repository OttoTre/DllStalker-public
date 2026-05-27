#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/static_instance_finder.h"

#include <algorithm>

#include "dumper/field_catalog.h"
#include "types/memory_guard.h"

namespace Engine::Dumper
{
StaticInstanceFinder::StaticInstanceFinder(UnityResolver& resolver, FieldCatalog& fields)
    : m_resolver(resolver)
    , m_fields(fields)
{
}

void* StaticInstanceFinder::FindStaticInstance(void* klass) {
    auto candidates = FindStaticInstanceCandidates(klass);
    return candidates.empty() ? nullptr : candidates.front();
}

std::vector<void*> StaticInstanceFinder::FindStaticInstanceCandidates(void* klass) {
    std::vector<void*> candidates;
    if (!klass || !m_resolver.module.exports.fnClassGetStaticFieldsPtr) return candidates;

    for (const auto& field : m_fields.GetRawFields(klass, nullptr)) {
        if (field.staticValue == 0) continue;

        void* slotAddress = reinterpret_cast<void*>(field.staticValue);
        if (!Memory::IsReadablePointer(slotAddress, sizeof(void*))) continue;

        void* instance = *reinterpret_cast<void**>(slotAddress);
        if (!instance || !Memory::IsReadablePointer(instance, sizeof(void*))) continue;
        if (*reinterpret_cast<void**>(instance) != klass) continue;

        if (std::find(candidates.begin(), candidates.end(), instance) == candidates.end())
            candidates.push_back(instance);
    }

    return candidates;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
