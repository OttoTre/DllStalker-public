#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/value_search/value_search_scanner.h"

#include "dumper/value_search/value_search_element_leaves.h"
#include "dumper/value_search/value_search_match.h"
#include "dumper/value_search/value_search_nested_leaves.h"
#include "dumper/value_search/value_search_ptr_follow.h"
#include "dumper/value_search/value_search_scan_expand.h"
#include "dumper/value_search/value_search_schema_cache.h"
#include "dumper/value_search/value_search_value_passes.h"
#include "types/type_classifier.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Engine::Dumper
{
namespace
{
bool FieldKindAllowed(const FieldInfo& field, const ValueSearchParams& params) {
    return ValueSearchFieldKindAllowed(field, params);
}

bool NamePasses(const FieldInfo& field, const ValueSearchParams& params) {
    return ValueSearchNamePasses(field.name, params);
}

ValueSearchHit MakeHit(const ValueSearchParams& params, void* instance, const FieldInfo& field) {
    ValueSearchHit hit{};
    hit.klass = params.klass;
    hit.instance = instance;
    hit.className = params.className;
    hit.fieldName = field.name;
    hit.typeName = field.type;
    hit.valueDisplay = field.valueDisplay.empty() ? std::string("-") : field.valueDisplay;
    hit.valueAddress = field.valueAddress;
    hit.isEnum = field.isEnum;
    return hit;
}

bool IsCollectionCategory(Types::TypeCategory cat) {
    return IsValueSearchCollectionCategory(cat);
}

// Drill: re-expand container and pick element by Search hit name "items[3]".
const FieldInfo* ResolveCollectionElement(UnityDumper& dumper,
                                          const std::vector<FieldInfo>& rawFields,
                                          const std::string& fieldName,
                                          FieldInfo& storage) {
    return ResolveCollectionElementSlot(dumper, rawFields, fieldName, storage);
}

const FieldInfo* ResolveSearchField(UnityDumper& dumper,
                                    void* ownerKlass,
                                    const std::vector<FieldInfo>& rawFields,
                                    const std::string& fieldName,
                                    FieldInfo& storage) {
    if (const FieldInfo* nested = ResolveFieldOrNestedLeaf(rawFields, fieldName, storage)) {
        return nested;
    }
    // Follow PTR: ptr.field / ptr.arr[i] / ptr.arr[i].member before bare
    // collection paths (avoids mistaking "a.b[0]" for a top-level container).
    if (const FieldInfo* ptrFollow =
            ResolvePtrFollowField(dumper, ownerKlass, rawFields, fieldName, storage)) {
        return ptrFollow;
    }
    // Deep: container[i].member before slot-only container[i].
    if (const FieldInfo* interior =
            ResolveCollectionElementInterior(dumper, ownerKlass, rawFields, fieldName, storage)) {
        return interior;
    }
    return ResolveCollectionElement(dumper, rawFields, fieldName, storage);
}
} // namespace

ValueSearchScanResult RunValueSearch(UnityDumper& dumper,
                                     const ValueSearchParams& params,
                                     const std::vector<void*>& instances,
                                     std::stop_token stopToken,
                                     ValueSearchSchemaCache* schemaCache,
                                     std::shared_ptr<const ValueSearchClassSchema> prebuiltSchema) {
    ValueSearchScanResult out{};
    if (!params.klass && !params.drillMode) {
        return out;
    }

    if (params.drillMode) {
        // Re-check baseline only; name filter ignored; value required.
        // Works across mixed-class Image hits via baseline.klass.
        if (params.valueNeedle.empty()) {
            return out;
        }
        std::unordered_map<void*, std::vector<FieldInfo>> fieldsByInstance;
        if (!params.baselineHits) {
            return out;
        }
        for (const auto& baseline : *params.baselineHits) {
            if (stopToken.stop_requested()) {
                break;
            }
            if (!baseline.instance || baseline.fieldName.empty()) {
                continue;
            }
            auto it = fieldsByInstance.find(baseline.instance);
            if (it == fieldsByInstance.end()) {
                void* const fieldKlass = baseline.klass ? baseline.klass : params.klass;
                if (!fieldKlass) {
                    continue;
                }
                std::vector<FieldInfo> fields;
                try {
                    fields = dumper.GetRawFields(fieldKlass, baseline.instance);
                }
                catch (...) {
                    fields.clear();
                }
                it = fieldsByInstance.emplace(baseline.instance, std::move(fields)).first;
                ++out.instancesScanned;
            }
            ++out.fieldsVisited;
            FieldInfo nestedStorage{};
            void* const ownerKlass = baseline.klass ? baseline.klass : params.klass;
            const FieldInfo* found =
                ResolveSearchField(dumper, ownerKlass, it->second, baseline.fieldName, nestedStorage);
            // Baseline slots already passed chips/name on Search — only value.
            if (!found || !ValuePasses(dumper, *found, params)) {
                continue;
            }
            ValueSearchHit hit = MakeHit(params, baseline.instance, *found);
            if (baseline.klass) {
                hit.klass = baseline.klass;
                hit.className = baseline.className.empty() ? hit.className : baseline.className;
            }
            out.hits.push_back(std::move(hit));
            if (out.hits.size() >= kValueSearchHitCap) {
                out.truncReason = ValueSearchTruncReason::HitCap;
                break;
            }
        }
        return out;
    }

    // Schema gate: skip instance walk when a complete schema says no field
    // can match chips + name. Incomplete builds leave the gate off.
    std::unordered_set<std::string> schemaNameAllow;
    bool useNameAllow = false;
    const ValueSearchClassSchema* liveSchema = nullptr;
    std::shared_ptr<const ValueSearchClassSchema> schemaKeepAlive{};
    auto buildNameAllow = [&](const ValueSearchClassSchema& schema) {
        if (params.nameNeedle.empty()) {
            return;
        }
        useNameAllow = true;
        for (const auto& field : schema.fields) {
            if (!SchemaFieldMayContribute(field, schema, params, /*forNameAllowSet=*/true)) {
                continue;
            }
            schemaNameAllow.insert(field.name);
        }
    };
    if (prebuiltSchema) {
        schemaKeepAlive = prebuiltSchema;
        liveSchema = schemaKeepAlive.get();
        buildNameAllow(*schemaKeepAlive);
    }
    else if (schemaCache) {
        const ValueSearchSchemaLookup lookup = schemaCache->GetOrBuild(dumper, params.klass);
        if (lookup.usable && lookup.schema) {
            if (!ClassSchemaMayMatch(*lookup.schema, params)) {
                out.classesSkippedBySchema = 1;
                return out;
            }
            schemaKeepAlive = lookup.schema;
            liveSchema = schemaKeepAlive.get();
            buildNameAllow(*schemaKeepAlive);
        }
    }

    for (void* instance : instances) {
        if (stopToken.stop_requested()) {
            break;
        }
        if (!instance) {
            continue;
        }
        std::vector<FieldInfo> fields;
        try {
            fields = dumper.GetRawFields(params.klass, instance);
        }
        catch (...) {
            continue;
        }
        // Synthesize Parent.Child scalar leaves for allowlisted Unity inlines
        // so Number/name gates apply without chip special-casing.
        ExpandAllowlistedNestedLeaves(fields);
        ++out.instancesScanned;
        size_t ptrFieldsFollowed = 0;
        for (const auto& field : fields) {
            if (stopToken.stop_requested()) {
                break;
            }
            ++out.fieldsVisited;
            if (IsCollectionCategory(Types::GetCategory(field.type))) {
                if (!SchemaNameAllowIncludesCollection(
                        field, schemaNameAllow, useNameAllow, params.chipDeep,
                        /*pathPrefix=*/{})) {
                    continue;
                }
                // ARRAY/LIST: expand elements (one level). Name filter: container
                // and/or Deep leaf patterns; element kinds use
                // ValueSearchElementKindAllowed (not nested ARRAY/LIST).
                TryMatchCollectionElements(
                    dumper, params, instance, params.klass, field, liveSchema,
                    /*pathPrefix=*/{}, stopToken, out);
                if (out.truncReason == ValueSearchTruncReason::HitCap) {
                    return out;
                }
                continue;
            }

            const bool followablePtr = params.chipDeep && IsFollowablePtrField(field);
            if (useNameAllow
                && !SchemaNameAllowContains(schemaNameAllow, field.name)
                && !(followablePtr
                     && SchemaNameAllowIncludesPtr(
                         field, schemaNameAllow, useNameAllow, params.chipDeep))) {
                continue;
            }

            // Direct field match (PTR slot null/non-null when chipPtr, etc.).
            if (FieldKindAllowed(field, params)
                && NamePasses(field, params)
                && ValuePasses(dumper, field, params)) {
                out.hits.push_back(MakeHit(params, instance, field));
                if (out.hits.size() >= kValueSearchHitCap) {
                    out.truncReason = ValueSearchTruncReason::HitCap;
                    return out;
                }
            }

            // Follow PTR (Deep): independent of chipPtr / slot match.
            if (followablePtr && ptrFieldsFollowed < kValueSearchMaxPtrFieldsFollowed) {
                if (TryMatchPtrFollow(
                        dumper, params, instance, field, liveSchema, schemaNameAllow,
                        useNameAllow, stopToken, out)) {
                    ++ptrFieldsFollowed;
                }
                if (out.truncReason == ValueSearchTruncReason::HitCap) {
                    return out;
                }
            }
        }
    }
    return out;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
