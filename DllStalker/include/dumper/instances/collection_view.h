#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "unity_resolver.h"
#include "types/dumper_types.h"

namespace Engine::Dumper
{
class ObjectIdentity;

// Search/Drill hit names: "items[3]". CollectionView row names stay "[3]"
// for Fields inspector; Search synthesizes container-prefixed names.
std::string FormatCollectionElementName(const std::string& containerName, size_t index);

// Parse "items[3]" → container + index. Rejects nested brackets / junk.
bool ParseCollectionElementName(const std::string& fieldName,
                                std::string& outContainer,
                                size_t& outIndex);

// True for synthesized element rows: "[n]" (Fields) or "container[n]" (Search).
bool IsSynthesizedCollectionElementName(const std::string& fieldName);

// Parse Fields inspector row name "[n]" → index. Accepts open==0 ("[3]").
// Rejects Search names "items[3]" — do not use ParseCollectionElementName
// for Fields [n] (that helper requires a non-empty container prefix).
bool ParseSynthesizedFieldsElementIndex(const std::string& fieldName, size_t& outIndex);

// Decomposes an array (T[]) or List<T> field into one synthesized FieldInfo
// per element. The Inspector renders the result with its existing field
// row code so reference elements stay clickable for the Walker.
// List path: resolves wrapper `_items` / `_size` by name (IL2CPP + Mono).
// maxElements: 0 = uncapped (Inspector / Fields walker); Search/Drill *scan*
// expand passes kValueSearchMaxCollectionElements; resolve/nav passes
// index+1 so hits past the scan cap still resolve.
class CollectionView
{
public:
    CollectionView(UnityResolver& resolver, const ObjectIdentity& identity);

    // outEmptyButReadable: set when the header is readable and logical
    // length is 0 (no dummy [i] rows). Nullptr skips the report (Search).
    std::vector<FieldInfo> GetCollectionView(const FieldInfo& field,
                                             size_t maxElements = 0,
                                             bool* outEmptyButReadable = nullptr) const;

    // Re-resolve the live Array/List element buffer for `collectionField` and
    // confirm `elementAddress` still falls on an in-range element slot.
    // Refuses null / unreadable / shrunk collections with a GUI-friendly error.
    bool IsAddressInLiveElementBuffer(const FieldInfo& collectionField,
                                      uintptr_t elementAddress,
                                      std::string* error = nullptr) const;

    // Grow a List<T> logical size by one only when its existing backing array
    // has spare capacity. Does not initialize the newly exposed slot.
    bool TryWriteListLogicalSize(const FieldInfo& collectionField,
                                 std::string* error = nullptr) const;

    // Element klass for ARRAY/LIST. Live path uses the array object behind
    // field.valueAddress (same as GetCollectionView). Metadata path
    // (schema / Deep): ownerKlass + field name → field type → element
    // (List via inflated klass `_items` array type → GetElementClass).
    void* TryResolveElementKlass(const FieldInfo& field) const;
    void* TryResolveElementKlass(void* ownerKlass, const char* fieldName) const;

private:
    UnityResolver& m_resolver;
    const ObjectIdentity& m_identity;

    void* ElementKlassFromArrayBase(uintptr_t arrayBase) const;
    void* ElementKlassFromFieldType(void* fieldType) const;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
