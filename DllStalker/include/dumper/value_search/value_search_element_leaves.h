#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstddef>
#include <string>
#include <vector>

#include "types/dumper_types.h"
#include "types/value_search_schema.h"
#include "types/value_search_types.h"
#include "unity_dumper.h"

namespace Engine::Dumper
{
// Deep Search: Array/List element interior leaves (one hop only).
// Live hit names:  container[i].member  (e.g. cheats[0].name)
// Schema patterns: container[].member   (wildcard index for name/chip gate)
// Caps: kValueSearchMaxElementInteriorFields (32) per element; pair with
// kValueSearchMaxCollectionElements (64). No nested ARRAY/LIST / depth-2.
// Shared by schema Build, live Search expand, and Drill resolve.

std::string FormatCollectionElementInteriorName(const std::string& containerName,
                                                size_t index,
                                                const std::string& memberName);

std::string FormatCollectionElementInteriorSchemaName(const std::string& containerName,
                                                      const std::string& memberName);

// "cheats[0].name" → container / index / member. Rejects depth-2 and "[]".
bool ParseCollectionElementInteriorName(const std::string& fieldName,
                                        std::string& outContainer,
                                        size_t& outIndex,
                                        std::string& outMember);

// True for schema patterns "container[].member" (exactly one "[]" segment).
bool IsCollectionElementInteriorSchemaName(const std::string& fieldName);

// Schema name "cheats[].name" belongs to container "cheats".
bool CollectionInteriorSchemaBelongsTo(const std::string& schemaName,
                                       const std::string& containerName);

// Append container[].member schema rows for an ARRAY/LIST parent (metadata).
// Uses ownerKlass + parent.name → element klass → GetRawFields(metadataOnly).
// Caller keeps the parent entry; this only pushes leaves (cap 32, instance only).
void AppendCollectionElementInteriorSchemaLeaves(UnityDumper& dumper,
                                                 void* ownerKlass,
                                                 const ValueSearchFieldSchema& parent,
                                                 std::vector<ValueSearchFieldSchema>& out);

// Live: expand direct member fields of one collection element into leaves
// named container[i].member. Ref: managed ptr → instance klass → GetRawFields.
// Valuetype: element klass (CollectionView path) → fields at slot+offset.
// Does not treat dotted game struct type names as PTR for layout.
// maxFields defaults to kValueSearchMaxElementInteriorFields.
std::vector<FieldInfo> ExpandElementInteriorLeaves(UnityDumper& dumper,
                                                   void* ownerKlass,
                                                   const FieldInfo& container,
                                                   size_t elementIndex,
                                                   const FieldInfo& elementSlot,
                                                   size_t maxFields = kValueSearchMaxElementInteriorFields);

// Drill / resolve: parse container[i].member, re-expand, synthesize into storage.
// Returns &storage or nullptr. ownerKlass is the class that owns the container field.
const FieldInfo* ResolveCollectionElementInterior(UnityDumper& dumper,
                                                  void* ownerKlass,
                                                  const std::vector<FieldInfo>& rawFields,
                                                  const std::string& fieldName,
                                                  FieldInfo& storage);

// Drill: parse container[i], expand, rename to container[i]. Shared by root + Follow nested.
const FieldInfo* ResolveCollectionElementSlot(UnityDumper& dumper,
                                              const std::vector<FieldInfo>& rawFields,
                                              const std::string& fieldName,
                                              FieldInfo& storage);
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
