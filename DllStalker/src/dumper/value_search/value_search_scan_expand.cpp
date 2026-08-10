#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/value_search/value_search_scan_expand.h"

#include "dumper/instances/collection_view.h"
#include "dumper/value_search/value_search_element_leaves.h"
#include "dumper/value_search/value_search_match.h"
#include "dumper/value_search/value_search_nested_leaves.h"
#include "dumper/value_search/value_search_ptr_follow.h"
#include "dumper/value_search/value_search_value_passes.h"
#include "types/type_classifier.h"

#include <vector>

namespace Engine::Dumper
{
namespace
{
bool FieldKindAllowed(const FieldInfo& field, const ValueSearchParams& params) {
    return ValueSearchFieldKindAllowed(field, params);
}

bool ElementKindAllowed(const FieldInfo& field, const ValueSearchParams& params) {
    return ValueSearchElementKindAllowed(field, params);
}

bool NamePasses(const FieldInfo& field, const ValueSearchParams& params) {
    return ValueSearchNamePasses(field.name, params);
}

bool IsCollectionCategory(Types::TypeCategory cat) {
    return IsValueSearchCollectionCategory(cat);
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

// Name gate for entering a collection container.
// Deep + needle: container name OR any schema container[].* member that passes.
// pathPrefix (Follow PTR): schema/hit names use ptr.container; FieldInfo stays short.
// Unusable schema + Deep + name: do not admit-all collections.
bool ContainerNameAllowsExpand(const FieldInfo& container,
                               const ValueSearchParams& params,
                               const ValueSearchClassSchema* schema,
                               const std::string& pathPrefix,
                               bool* outContainerNameMatched) {
    if (outContainerNameMatched) {
        *outContainerNameMatched = false;
    }
    const std::string hitContainerName = pathPrefix.empty()
        ? container.name
        : FormatPtrFollowName(pathPrefix, container.name);
    if (params.nameNeedle.empty()) {
        if (outContainerNameMatched) {
            *outContainerNameMatched = true;
        }
        return true;
    }
    if (ValueSearchNamePasses(hitContainerName, params)) {
        if (outContainerNameMatched) {
            *outContainerNameMatched = true;
        }
        return true;
    }
    if (!params.chipDeep) {
        return false;
    }
    if (schema) {
        for (const auto& field : schema->fields) {
            if (!CollectionInteriorSchemaBelongsTo(field.name, hitContainerName)) {
                continue;
            }
            if (!ValueSearchNamePasses(field.name, params)) {
                continue;
            }
            // Leaf-justified expand only; container name did not match.
            return true;
        }
        return false;
    }
    // No usable schema: cannot prove a matching container[].member —
    // refuse expand (same as Deep-off when container name fails).
    return false;
}

// Name gate for entering a Follow-PTR hop (Deep only). No admit-all without
// evidence; leaf-justified via schema ptr.* patterns.
bool PtrNameAllowsFollow(const FieldInfo& ptrField,
                         const ValueSearchParams& params,
                         const ValueSearchClassSchema* schema,
                         bool* outPtrNameMatched) {
    if (outPtrNameMatched) {
        *outPtrNameMatched = false;
    }
    if (params.nameNeedle.empty()) {
        if (outPtrNameMatched) {
            *outPtrNameMatched = true;
        }
        return true;
    }
    if (ValueSearchNamePasses(ptrField.name, params)) {
        if (outPtrNameMatched) {
            *outPtrNameMatched = true;
        }
        return true;
    }
    if (!params.chipDeep) {
        return false;
    }
    if (schema) {
        for (const auto& field : schema->fields) {
            if (!PtrFollowSchemaBelongsTo(field.name, ptrField.name)) {
                continue;
            }
            if (IsPtrFollowUnresolvedSchemaName(field.name)) {
                // Unknown nested shape — cannot leaf-justify a specific name.
                continue;
            }
            if (!ValueSearchNamePasses(field.name, params)) {
                continue;
            }
            return true;
        }
        return false;
    }
    return false;
}
} // namespace

// Schema name-allow early-out for collection fields when Deep may match
// container[].member patterns without the bare container name in the set.
// pathPrefix: Follow PTR uses ptr.container keys in schemaNameAllow.
bool SchemaNameAllowIncludesCollection(const FieldInfo& container,
                                       const std::unordered_set<std::string>& schemaNameAllow,
                                       bool useNameAllow,
                                       bool chipDeep,
                                       const std::string& pathPrefix) {
    if (!useNameAllow) {
        return true;
    }
    const std::string hitContainerName = pathPrefix.empty()
        ? container.name
        : FormatPtrFollowName(pathPrefix, container.name);
    if (SchemaNameAllowContains(schemaNameAllow, hitContainerName)) {
        return true;
    }
    if (!chipDeep) {
        return false;
    }
    const std::string prefix = hitContainerName + "[].";
    for (const auto& name : schemaNameAllow) {
        if (name.size() > prefix.size()
            && name.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }
    return false;
}

// Early-out for Follow: bare PTR name in allow (Deep, no chipPtr required) or
// nested ptr.* prefix. Empty needle → useNameAllow false (admit). Sentinel
// ptr.* is never in the allow set.
bool SchemaNameAllowIncludesPtr(const FieldInfo& ptrField,
                                const std::unordered_set<std::string>& schemaNameAllow,
                                bool useNameAllow,
                                bool chipDeep) {
    if (!useNameAllow) {
        return true;
    }
    if (SchemaNameAllowContains(schemaNameAllow, ptrField.name)) {
        return true;
    }
    if (!chipDeep) {
        return false;
    }
    const std::string prefix = ptrField.name + ".";
    for (const auto& name : schemaNameAllow) {
        if (name.size() > prefix.size()
            && name.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }
    return false;
}

// Expand ARRAY/LIST one level (cap kValueSearchMaxCollectionElements).
// Deep: also emit container[i].member leaves (no nested collections / depth-2).
// pathPrefix: Follow PTR hit names; container FieldInfo keeps the short name.
bool TryMatchCollectionElements(UnityDumper& dumper,
                                const ValueSearchParams& params,
                                void* instance,
                                void* ownerKlass,
                                const FieldInfo& container,
                                const ValueSearchClassSchema* schema,
                                const std::string& pathPrefix,
                                std::stop_token stopToken,
                                ValueSearchScanResult& out) {
    const auto cat = Types::GetCategory(container.type);
    if (!IsCollectionCategory(cat)) {
        return false;
    }
    // Kind gate on the container (Deep implies Array/List).
    if (!FieldKindAllowed(container, params)) {
        return true; // handled (skip)
    }
    bool containerNameMatched = false;
    if (!ContainerNameAllowsExpand(
            container, params, schema, pathPrefix, &containerNameMatched)) {
        return true;
    }
    // Slots: Deep-off (or empty needle / container name match) keep existing
    // behavior — all slots after container gate. Deep + leaf-justified expand
    // (container name missed, schema member matched): interiors only.
    const bool emitSlots =
        params.nameNeedle.empty() || containerNameMatched;

    std::vector<FieldInfo> elements;
    try {
        elements = dumper.GetCollectionView(container, kValueSearchMaxCollectionElements);
    }
    catch (...) {
        return true;
    }

    const bool deepInterior = params.chipDeep;

    const std::string hitContainerName = pathPrefix.empty()
        ? container.name
        : FormatPtrFollowName(pathPrefix, container.name);

    for (size_t i = 0; i < elements.size(); ++i) {
        if (stopToken.stop_requested()) {
            break;
        }
        ++out.fieldsVisited;
        FieldInfo element = elements[i];
        element.name = FormatCollectionElementName(hitContainerName, i);
        // Element kinds: Number/String/Bool/Enum/Ptr/inline only — not
        // ARRAY/LIST (one-level; no nested collection expand).
        if (emitSlots
            && ElementKindAllowed(element, params)
            && ValuePasses(dumper, element, params)) {
            out.hits.push_back(MakeHit(params, instance, element));
            if (out.hits.size() >= kValueSearchHitCap) {
                out.truncReason = ValueSearchTruncReason::HitCap;
                break;
            }
        }

        if (!deepInterior) {
            continue;
        }
        if (stopToken.stop_requested()) {
            break;
        }

        std::vector<FieldInfo> leaves = ExpandElementInteriorLeaves(
            dumper, ownerKlass, container, i, elements[i],
            kValueSearchMaxElementInteriorFields);
        for (auto& leaf : leaves) {
            if (stopToken.stop_requested()) {
                break;
            }
            ++out.fieldsVisited;
            if (!pathPrefix.empty()) {
                // Expand used short container name; rewrite to ptr.container[i].member.
                leaf.name = FormatPtrFollowName(pathPrefix, leaf.name);
            }
            // Interior leaves: same element kind gate (no nested ARRAY/LIST).
            if (!ElementKindAllowed(leaf, params)) {
                continue;
            }
            if (!NamePasses(leaf, params)) {
                continue;
            }
            if (!ValuePasses(dumper, leaf, params)) {
                continue;
            }
            out.hits.push_back(MakeHit(params, instance, leaf));
            if (out.hits.size() >= kValueSearchHitCap) {
                out.truncReason = ValueSearchTruncReason::HitCap;
                break;
            }
        }
        if (out.truncReason == ValueSearchTruncReason::HitCap) {
            break;
        }
    }
    return true;
}

// Follow PTR (Deep): one hop into an instance PTR field; same match pipeline
// on the nested object. No PTR→PTR. Returns true when the target was resolved.
bool TryMatchPtrFollow(UnityDumper& dumper,
                       const ValueSearchParams& params,
                       void* rootInstance,
                       const FieldInfo& ptrField,
                       const ValueSearchClassSchema* schema,
                       const std::unordered_set<std::string>& schemaNameAllow,
                       bool useNameAllow,
                       std::stop_token stopToken,
                       ValueSearchScanResult& out) {
    if (!params.chipDeep || !IsFollowablePtrField(ptrField)) {
        return false;
    }
    if (!SchemaNameAllowIncludesPtr(ptrField, schemaNameAllow, useNameAllow, params.chipDeep)) {
        return false;
    }
    bool ptrNameMatched = false;
    if (!PtrNameAllowsFollow(ptrField, params, schema, &ptrNameMatched)) {
        return false;
    }
    // Bare PTR name match (or empty needle): skip nested schemaNameAllow
    // exact-find — allow has the PTR slot (or useNameAllow is off), not
    // every live ptr.leaf; sentinel ptr.* is never inserted. Mirror Array
    // Deep containerNameMatched: still kind chips + NamePasses + ValuePasses.
    // Leaf-justified Follow (nested schema prefix only): keep allow filter.
    const bool filterNestedByNameAllow = useNameAllow && !ptrNameMatched;

    void* nestedInstance = nullptr;
    void* nestedKlass = nullptr;
    if (!TryResolvePtrTarget(dumper, ptrField, nestedInstance, nestedKlass)) {
        return false; // null / unreadable — skip, do not consume cap
    }

    std::vector<FieldInfo> fields;
    try {
        fields = dumper.GetRawFields(nestedKlass, nestedInstance);
    }
    catch (...) {
        return true; // resolved but unreadable fields — still consumed a hop
    }
    ExpandAllowlistedNestedLeaves(fields);

    const std::string& pathPrefix = ptrField.name;
    // Hits stay rooted on the scanned instance (Drill re-resolves via path).
    for (const auto& field : fields) {
        if (stopToken.stop_requested()) {
            break;
        }
        ++out.fieldsVisited;
        if (IsCollectionCategory(Types::GetCategory(field.type))) {
            if (filterNestedByNameAllow
                && !SchemaNameAllowIncludesCollection(
                    field, schemaNameAllow, useNameAllow, params.chipDeep,
                    pathPrefix)) {
                continue;
            }
            TryMatchCollectionElements(
                dumper, params, rootInstance, nestedKlass, field, schema,
                pathPrefix, stopToken, out);
            if (out.truncReason == ValueSearchTruncReason::HitCap) {
                return true;
            }
            continue;
        }
        FieldInfo hitField = field;
        hitField.name = FormatPtrFollowName(pathPrefix, field.name);
        if (filterNestedByNameAllow
            && !SchemaNameAllowContains(schemaNameAllow, hitField.name)) {
            continue;
        }
        if (!FieldKindAllowed(hitField, params)) {
            continue;
        }
        if (!NamePasses(hitField, params)) {
            continue;
        }
        if (!ValuePasses(dumper, hitField, params)) {
            continue;
        }
        out.hits.push_back(MakeHit(params, rootInstance, hitField));
        if (out.hits.size() >= kValueSearchHitCap) {
            out.truncReason = ValueSearchTruncReason::HitCap;
            return true;
        }
    }
    return true;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
