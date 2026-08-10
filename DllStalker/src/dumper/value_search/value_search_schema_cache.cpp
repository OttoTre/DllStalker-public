#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/value_search/value_search_schema_cache.h"

#include "dumper/value_search/value_search_element_leaves.h"
#include "dumper/value_search/value_search_match.h"
#include "dumper/value_search/value_search_nested_leaves.h"
#include "dumper/value_search/value_search_ptr_follow.h"
#include "types/type_classifier.h"
#include "unity_dumper.h"

namespace Engine::Dumper
{
bool SchemaFieldMayContribute(const ValueSearchFieldSchema& field,
                              const ValueSearchClassSchema& schema,
                              const ValueSearchParams& params,
                              bool forNameAllowSet) {
    (void)schema;
    if (IsPtrFollowUnresolvedSchemaName(field.name)) {
        return !forNameAllowSet && params.chipDeep;
    }
    if (IsCollectionElementInteriorSchemaName(field.name)) {
        if (!params.chipDeep) {
            return false;
        }
    }
    else if (field.isPtrFollow) {
        if (!params.chipDeep) {
            return false;
        }
    }
    const bool followablePtrSlot =
        params.chipDeep && IsFollowablePtrSchemaField(field);
    if (!followablePtrSlot && !ValueSearchFieldKindAllowed(field, params)) {
        return false;
    }
    return ValueSearchNamePasses(field.name, params);
}

bool ClassSchemaMayMatch(const ValueSearchClassSchema& schema, const ValueSearchParams& params) {
    for (const auto& field : schema.fields) {
        if (SchemaFieldMayContribute(field, schema, params, /*forNameAllowSet=*/false)) {
            return true;
        }
    }
    return false;
}

ValueSearchSchemaCache::BuildResult ValueSearchSchemaCache::Build(UnityDumper& dumper,
                                                                  void* klass) const {
    BuildResult result{};
    result.schema.klass = klass;
    if (!klass) {
        result.ok = true;
        return result;
    }

    std::vector<FieldInfo> raw;
    bool enumerationComplete = true;
    try {
        // Metadata-only; incomplete builds must not be cached.
        raw = dumper.GetRawFields(klass, nullptr, &enumerationComplete,
                                  /*metadataOnly=*/true);
    }
    catch (...) {
        result.ok = false;
        return result;
    }
    if (!enumerationComplete) {
        result.ok = false;
        return result;
    }

    // Parent nested leaves + Deep interiors + Follow PTR patterns.
    result.schema.fields.reserve(raw.size() * 5);
    for (auto& field : raw) {
        ValueSearchFieldSchema entry{};
        entry.name = std::move(field.name);
        entry.type = std::move(field.type);
        entry.offset = field.offset;
        entry.isStatic = field.isStatic;
        entry.isEnum = field.isEnum;
        result.schema.fields.push_back(std::move(entry));
        const size_t parentIndex = result.schema.fields.size() - 1;
        AppendAllowlistedNestedSchemaLeaves(result.schema.fields[parentIndex],
                                            result.schema.fields);
        const auto parentCat = Types::GetCategory(result.schema.fields[parentIndex].type);
        if (parentCat == Types::TypeCategory::ARRAY
            || parentCat == Types::TypeCategory::LIST) {
            AppendCollectionElementInteriorSchemaLeaves(
                dumper, klass, result.schema.fields[parentIndex], result.schema.fields);
        }
        else if (IsFollowablePtrSchemaField(result.schema.fields[parentIndex])) {
            AppendPtrFollowSchemaLeaves(
                dumper, klass, result.schema.fields[parentIndex], result.schema.fields);
        }
    }
    result.ok = true;
    return result;
}

ValueSearchSchemaLookup ValueSearchSchemaCache::GetOrBuild(UnityDumper& dumper, void* klass) {
    ValueSearchSchemaLookup lookup{};
    if (!klass) {
        lookup.usable = true;
        return lookup;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_byKlass.find(klass);
        if (it != m_byKlass.end()) {
            lookup.schema = it->second;
            lookup.usable = true;
            return lookup;
        }
    }

    BuildResult built = Build(dumper, klass);
    if (!built.ok) {
        // Transient / incomplete — leave map untouched; caller must not skip.
        lookup.usable = false;
        return lookup;
    }

    auto held = std::make_shared<const ValueSearchClassSchema>(std::move(built.schema));
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto [it, inserted] = m_byKlass.emplace(klass, std::move(held));
    lookup.schema = it->second;
    lookup.usable = true;
    return lookup;
}

void ValueSearchSchemaCache::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_byKlass.clear();
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
