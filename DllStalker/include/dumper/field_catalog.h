#pragma once

#ifdef ENABLE_DUMPER

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
    std::vector<FieldInfo> GetRawFields(void* klass, void* instancePtr);

    // Forwards to Engine::Write::SetFieldValue. Lives here so callers
    // talk to the dumper layer instead of reaching into types/.
    bool SetFieldValue(const FieldInfo& field, const std::string& newValue, std::string* error = nullptr);

private:
    UnityResolver& m_resolver;
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
