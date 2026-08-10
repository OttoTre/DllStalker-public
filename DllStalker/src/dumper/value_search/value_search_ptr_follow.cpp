#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/value_search/value_search_ptr_follow.h"

#include "dumper/instances/collection_view.h"
#include "dumper/value_search/value_search_element_leaves.h"
#include "dumper/value_search/value_search_match.h"
#include "dumper/value_search/value_search_nested_leaves.h"
#include "types/memory_guard.h"
#include "types/type_classifier.h"

namespace Engine::Dumper
{
namespace
{
bool IsCollectionCategory(Types::TypeCategory cat) {
    return IsValueSearchCollectionCategory(cat);
}

void PrefixSchemaLeafNames(std::vector<ValueSearchFieldSchema>& out,
                           size_t beginIndex,
                           const std::string& ptrFieldName) {
    for (size_t i = beginIndex; i < out.size(); ++i) {
        out[i].name = FormatPtrFollowName(ptrFieldName, out[i].name);
        out[i].isPtrFollow = true;
    }
}

void PushUnresolvedPtrSentinel(const ValueSearchFieldSchema& ptrParent,
                               std::vector<ValueSearchFieldSchema>& out) {
    ValueSearchFieldSchema sentinel{};
    sentinel.name = FormatPtrFollowName(ptrParent.name, "*");
    sentinel.type = "System.Object";
    sentinel.offset = ptrParent.offset;
    sentinel.isStatic = false;
    sentinel.isEnum = false;
    sentinel.isPtrFollow = true;
    out.push_back(std::move(sentinel));
}

// Drill: re-expand container and pick element by "items[3]" (unprefixed nested).
// (Uses shared ResolveCollectionElementSlot — see value_search_element_leaves.)

const FieldInfo* ResolveNestedSearchField(UnityDumper& dumper,
                                          void* nestedKlass,
                                          const std::vector<FieldInfo>& rawFields,
                                          const std::string& fieldName,
                                          FieldInfo& storage) {
    // No second PTR hop. Nested path only: leaf / arr[i] / arr[i].member.
    if (const FieldInfo* nested = ResolveFieldOrNestedLeaf(rawFields, fieldName, storage)) {
        return nested;
    }
    if (const FieldInfo* interior =
            ResolveCollectionElementInterior(dumper, nestedKlass, rawFields, fieldName, storage)) {
        return interior;
    }
    return ResolveCollectionElementSlot(dumper, rawFields, fieldName, storage);
}
} // namespace

std::string FormatPtrFollowName(const std::string& ptrFieldName,
                                const std::string& nestedName) {
    if (ptrFieldName.empty()) {
        return nestedName;
    }
    if (nestedName.empty()) {
        return ptrFieldName;
    }
    return ptrFieldName + "." + nestedName;
}

bool ParsePtrFollowName(const std::string& fieldName,
                        std::string& outPtrField,
                        std::string& outNested) {
    // First '.' splits ptr slot from nested path (supports ptr.field,
    // ptr.arr[i], ptr.arr[i].member). Reject bracket-only / empty parts.
    const size_t dot = fieldName.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= fieldName.size()) {
        return false;
    }
    const std::string ptr = fieldName.substr(0, dot);
    if (ptr.find('[') != std::string::npos || ptr.find(']') != std::string::npos) {
        return false;
    }
    const std::string nested = fieldName.substr(dot + 1);
    if (nested.empty()) {
        return false;
    }
    outPtrField = ptr;
    outNested = nested;
    return true;
}

bool PtrFollowSchemaBelongsTo(const std::string& schemaName,
                              const std::string& ptrFieldName) {
    if (ptrFieldName.empty() || schemaName.size() <= ptrFieldName.size() + 1) {
        return false;
    }
    if (schemaName.compare(0, ptrFieldName.size(), ptrFieldName) != 0) {
        return false;
    }
    return schemaName[ptrFieldName.size()] == '.';
}

bool IsPtrFollowUnresolvedSchemaName(const std::string& fieldName) {
    // "codeLibrary.*" — exactly one trailing ".*" after a plain field name.
    if (fieldName.size() < 3 || fieldName[fieldName.size() - 2] != '.'
        || fieldName.back() != '*') {
        return false;
    }
    const std::string ptr = fieldName.substr(0, fieldName.size() - 2);
    return !ptr.empty()
        && ptr.find('.') == std::string::npos
        && ptr.find('[') == std::string::npos;
}

bool IsPtrFollowSchemaName(const std::string& fieldName,
                           const ValueSearchClassSchema& schema) {
    if (IsPtrFollowUnresolvedSchemaName(fieldName)) {
        return true;
    }
    std::string ptrField;
    std::string nested;
    if (!ParsePtrFollowName(fieldName, ptrField, nested)) {
        return false;
    }
    for (const auto& field : schema.fields) {
        if (field.name != ptrField) {
            continue;
        }
        return IsFollowablePtrSchemaField(field);
    }
    return false;
}

bool IsFollowablePtrField(const FieldInfo& field) {
    if (field.isEnum || field.isStatic) {
        return false;
    }
    return Types::GetCategory(field.type) == Types::TypeCategory::PTR;
}

bool IsFollowablePtrSchemaField(const ValueSearchFieldSchema& field) {
    if (field.isEnum || field.isStatic) {
        return false;
    }
    // Nested / sentinel patterns are not root followable slots.
    if (field.name.find('.') != std::string::npos) {
        return false;
    }
    return Types::GetCategory(field.type) == Types::TypeCategory::PTR;
}

bool TryResolvePtrTarget(UnityDumper& dumper,
                         const FieldInfo& ptrField,
                         void*& outInstance,
                         void*& outKlass) {
    outInstance = nullptr;
    outKlass = nullptr;
    if (!ptrField.hasValue || ptrField.valueAddress == 0) {
        return false;
    }
    uintptr_t nestedInstanceRaw = 0;
    if (!Memory::TryReadValue<uintptr_t>(ptrField.valueAddress, nestedInstanceRaw)
        || nestedInstanceRaw == 0) {
        return false;
    }
    void* nestedInstance = reinterpret_cast<void*>(nestedInstanceRaw);
    void* nestedKlass = nullptr;
    const std::string nestedClassName =
        dumper.TryGetClassNameFromInstance(nestedInstance, &nestedKlass);
    if (nestedClassName.empty() || !nestedKlass) {
        return false;
    }
    outInstance = nestedInstance;
    outKlass = nestedKlass;
    return true;
}

void AppendPtrFollowSchemaLeaves(UnityDumper& dumper,
                                 void* ownerKlass,
                                 const ValueSearchFieldSchema& ptrParent,
                                 std::vector<ValueSearchFieldSchema>& out) {
    if (!ownerKlass || !IsFollowablePtrSchemaField(ptrParent)) {
        return;
    }
    const std::string ptrName = ptrParent.name;
    void* typeKlass = dumper.TryResolveFieldTypeKlass(ownerKlass, ptrName.c_str());
    if (!typeKlass) {
        PushUnresolvedPtrSentinel(ptrParent, out);
        return;
    }

    std::vector<FieldInfo> raw;
    bool enumerationComplete = true;
    try {
        raw = dumper.GetRawFields(typeKlass, nullptr, &enumerationComplete,
                                  /*metadataOnly=*/true);
    }
    catch (...) {
        PushUnresolvedPtrSentinel(ptrParent, out);
        return;
    }
    if (!enumerationComplete) {
        PushUnresolvedPtrSentinel(ptrParent, out);
        return;
    }

    for (const auto& field : raw) {
        if (field.isStatic) {
            continue;
        }
        ValueSearchFieldSchema entry{};
        entry.name = FormatPtrFollowName(ptrName, field.name);
        entry.type = field.type;
        entry.offset = field.offset;
        entry.isStatic = false;
        entry.isEnum = field.isEnum;
        entry.isPtrFollow = true;
        out.push_back(std::move(entry));

        // Allowlisted Parent.Child under nested object — emit short then prefix.
        {
            ValueSearchFieldSchema shortParent{};
            shortParent.name = field.name;
            shortParent.type = field.type;
            shortParent.offset = field.offset;
            shortParent.isStatic = false;
            shortParent.isEnum = field.isEnum;
            const size_t before = out.size();
            AppendAllowlistedNestedSchemaLeaves(shortParent, out);
            PrefixSchemaLeafNames(out, before, ptrName);
        }

        // Deep collection interiors: short container name for klass resolve,
        // then prefix emitted container[].member → ptr.container[].member.
        const auto cat = Types::GetCategory(field.type);
        if (cat == Types::TypeCategory::ARRAY || cat == Types::TypeCategory::LIST) {
            ValueSearchFieldSchema shortParent{};
            shortParent.name = field.name;
            shortParent.type = field.type;
            shortParent.offset = field.offset;
            shortParent.isStatic = false;
            shortParent.isEnum = field.isEnum;
            const size_t before = out.size();
            AppendCollectionElementInteriorSchemaLeaves(dumper, typeKlass, shortParent, out);
            PrefixSchemaLeafNames(out, before, ptrName);
        }
    }
}

const FieldInfo* ResolvePtrFollowField(UnityDumper& dumper,
                                       void* ownerKlass,
                                       const std::vector<FieldInfo>& rawFields,
                                       const std::string& fieldName,
                                       FieldInfo& storage) {
    (void)ownerKlass;
    std::string ptrName;
    std::string nestedPath;
    if (!ParsePtrFollowName(fieldName, ptrName, nestedPath)) {
        return nullptr;
    }
    // Sentinel schema names are not live hit paths.
    if (nestedPath == "*") {
        return nullptr;
    }

    const FieldInfo* ptrField = nullptr;
    for (const auto& field : rawFields) {
        if (field.name == ptrName) {
            ptrField = &field;
            break;
        }
    }
    if (!ptrField || !IsFollowablePtrField(*ptrField)) {
        return nullptr;
    }

    void* nestedInstance = nullptr;
    void* nestedKlass = nullptr;
    if (!TryResolvePtrTarget(dumper, *ptrField, nestedInstance, nestedKlass)) {
        return nullptr;
    }

    std::vector<FieldInfo> nestedFields;
    try {
        nestedFields = dumper.GetRawFields(nestedKlass, nestedInstance);
    }
    catch (...) {
        return nullptr;
    }
    ExpandAllowlistedNestedLeaves(nestedFields);

    FieldInfo nestedStorage{};
    const FieldInfo* found =
        ResolveNestedSearchField(dumper, nestedKlass, nestedFields, nestedPath, nestedStorage);
    if (!found) {
        return nullptr;
    }
    storage = *found;
    // Drill baseline path is authoritative (ptr.nested...).
    storage.name = FormatPtrFollowName(ptrName, nestedPath);
    return &storage;
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
