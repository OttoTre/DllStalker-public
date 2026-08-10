#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <string>
#include <vector>

#include "unity_resolver.h"
#include "types/dumper_types.h"

namespace Engine::Dumper
{
// Reads instance and static field metadata off of a class descriptor.
// The "instance" overload also resolves each field's live address so the
// Inspector can render and (where supported) edit values in place.
class FieldCatalog
{
public:
    explicit FieldCatalog(UnityResolver& resolver);

    std::vector<FieldInfo> GetRawFields(void* klass);
    // enumerationComplete: when non-null, set false if class_get_fields aborted
    // mid-loop (SEH); true if enumeration finished normally (including empty).
    // metadataOnly: skip static blob resolve + value decode (schema / name gate).
    std::vector<FieldInfo> GetRawFields(void* klass, void* instancePtr,
                                        bool* enumerationComplete = nullptr,
                                        bool metadataOnly = false);

    // Metadata-only: static field storage addresses (no type/enum/decode).
    // Used by StaticInstanceFinder.
    std::vector<uintptr_t> EnumerateStaticFieldAddresses(void* klass);

    std::vector<EnumLiteral> GetEnumLiterals(void* enumKlass);

    bool SetFieldValue(const FieldInfo& field, const std::string& newValue, std::string* error = nullptr);

    // Metadata: ownerKlass + field name → Il2Cpp type → klass (best-effort).
    // Used by Value Search Follow PTR schema (nested type under a PTR slot).
    void* TryResolveFieldTypeKlass(void* ownerKlass, const char* fieldName) const;

private:
    UnityResolver& m_resolver;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
