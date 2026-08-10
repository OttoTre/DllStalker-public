#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstddef>
#include <memory>
#include <stop_token>
#include <vector>

#include "types/value_search_schema.h"
#include "types/value_search_types.h"
#include "unity_dumper.h"

namespace Engine::Dumper
{
class ValueSearchSchemaCache;

// Class-scoped field walk (no gui/). Name: fuzzy/strict; strings: quote-strip +
// case; Deep → collection expand + interiors + one PTR hop. Schema may skip
// classes that cannot match chips/name.
// prebuiltSchema: optional schema from caller GetOrBuild (avoids a second build).
ValueSearchScanResult RunValueSearch(UnityDumper& dumper,
                                     const ValueSearchParams& params,
                                     const std::vector<void*>& instances,
                                     std::stop_token stopToken,
                                     ValueSearchSchemaCache* schemaCache = nullptr,
                                     std::shared_ptr<const ValueSearchClassSchema> prebuiltSchema = {});
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
