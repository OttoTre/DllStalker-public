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
// Follow PTR / object graph: one hop through instance PTR fields when Deep
// is on. Hit names: ptr.field / ptr.arr[i] / ptr.arr[i].member.
// Schema: ptr.field / ptr.arr[].member; unresolved type → ptr.* sentinel.
// Caps: kValueSearchMaxPtrFieldsFollowed (16); no PTR→PTR chaining.
// Nested object reuses Array/List + Deep caps (64×32). Ptr chip stays
// null/non-null on PTR slots — following is Deep, not chipPtr.

// "codeLibrary" + "cheats[0].name" → "codeLibrary.cheats[0].name"
std::string FormatPtrFollowName(const std::string& ptrFieldName,
                                const std::string& nestedName);

// Split "codeLibrary.cheats[0].name" → ptr="codeLibrary", nested="cheats[0].name".
// Rejects empty segments; requires a single top-level '.' separator (first '.').
bool ParsePtrFollowName(const std::string& fieldName,
                        std::string& outPtrField,
                        std::string& outNested);

// Schema / live: name is under ptrField ("codeLibrary.cheats" / "...[].name").
bool PtrFollowSchemaBelongsTo(const std::string& schemaName,
                              const std::string& ptrFieldName);

// True for unresolved-type sentinel "ptrField.*" (conservative may-match).
bool IsPtrFollowUnresolvedSchemaName(const std::string& fieldName);

// True when schemaName is a Follow-PTR nested pattern (first segment is a
// root PTR field in schema). Excludes bare root PTR slots and Parent.Child
// when the parent is not PTR.
bool IsPtrFollowSchemaName(const std::string& fieldName,
                           const ValueSearchClassSchema& schema);

// Instance PTR slot eligible for follow: not enum, not static, category PTR
// (ARRAY/LIST are separate categories — not followed here).
bool IsFollowablePtrField(const FieldInfo& field);
bool IsFollowablePtrSchemaField(const ValueSearchFieldSchema& field);

// Walker-honest resolve: read PTR slot → nested instance + klass via
// TryGetClassNameFromInstance (null / unreadable / unknown → false).
bool TryResolvePtrTarget(UnityDumper& dumper,
                         const FieldInfo& ptrField,
                         void*& outInstance,
                         void*& outKlass);

// Schema Build: emit nested patterns under a followable PTR parent.
// Resolves type klass best-effort; on failure emits ptr.* sentinel.
void AppendPtrFollowSchemaLeaves(UnityDumper& dumper,
                                 void* ownerKlass,
                                 const ValueSearchFieldSchema& ptrParent,
                                 std::vector<ValueSearchFieldSchema>& out);

// Drill / resolve: parse ptr.nested, re-resolve PTR target, then nested path
// (field / arr[i] / arr[i].member) against the nested object's fields.
// Returns &storage or nullptr. ownerKlass owns the PTR field.
const FieldInfo* ResolvePtrFollowField(UnityDumper& dumper,
                                       void* ownerKlass,
                                       const std::vector<FieldInfo>& rawFields,
                                       const std::string& fieldName,
                                       FieldInfo& storage);
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
