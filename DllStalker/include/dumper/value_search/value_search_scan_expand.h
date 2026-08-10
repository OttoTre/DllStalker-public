#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <stop_token>
#include <string>
#include <unordered_set>

#include "types/dumper_types.h"
#include "types/value_search_schema.h"
#include "types/value_search_types.h"
#include "unity_dumper.h"

namespace Engine::Dumper
{
bool SchemaNameAllowIncludesCollection(const FieldInfo& container,
                                       const std::unordered_set<std::string>& schemaNameAllow,
                                       bool useNameAllow,
                                       bool chipDeep,
                                       const std::string& pathPrefix);

bool SchemaNameAllowIncludesPtr(const FieldInfo& ptrField,
                                const std::unordered_set<std::string>& schemaNameAllow,
                                bool useNameAllow,
                                bool chipDeep);

bool TryMatchCollectionElements(UnityDumper& dumper,
                                const ValueSearchParams& params,
                                void* instance,
                                void* ownerKlass,
                                const FieldInfo& container,
                                const ValueSearchClassSchema* schema,
                                const std::string& pathPrefix,
                                std::stop_token stopToken,
                                ValueSearchScanResult& out);

bool TryMatchPtrFollow(UnityDumper& dumper,
                       const ValueSearchParams& params,
                       void* rootInstance,
                       const FieldInfo& ptrField,
                       const ValueSearchClassSchema* schema,
                       const std::unordered_set<std::string>& schemaNameAllow,
                       bool useNameAllow,
                       std::stop_token stopToken,
                       ValueSearchScanResult& out);
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
