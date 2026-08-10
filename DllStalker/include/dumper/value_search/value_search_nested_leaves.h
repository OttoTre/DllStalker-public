#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstddef>
#include <string>
#include <vector>

#include "types/dumper_types.h"
#include "types/type_classifier.h"
#include "types/value_search_schema.h"

namespace Engine::Dumper
{
// Nested leaves for allowlisted Unity inline value structs.
// Component names match Unity field order (product lock):
//   VEC2: x,y | VEC3: x,y,z | VEC4/QUAT: x,y,z,w
//   COLOR/COLOR32: r,g,b,a | RECT: x,y,width,height
// Types: System.Single (float comps) / System.Byte (COLOR32).
// Offset: parent.offset + index * sizeof(component); isStatic from parent.
// Allowlisted Unity components only; depth capped at 1 (no deep recurse).

struct NestedLeafLayout {
    const char* name;     // "x", "r", "width", ...
    const char* typeName; // "System.Single" or "System.Byte"
    size_t      index;
    size_t      stride;   // sizeof component in parent layout
};

// Returns leaf table for cat, or nullptr / outCount=0 when not expandable.
const NestedLeafLayout* GetAllowlistedNestedLeaves(Types::TypeCategory cat,
                                                   size_t& outCount);

// Append Parent.Child schema rows for an allowlisted inline parent.
// Caller keeps the parent entry; this only pushes leaves.
void AppendAllowlistedNestedSchemaLeaves(const ValueSearchFieldSchema& parent,
                                         std::vector<ValueSearchFieldSchema>& out);

// For each existing IsInlineValueStruct parent in `fields`, append matching
// Parent.Child FieldInfo leaves (parents preserved). Decodes scalar display
// when the parent has a resolved valueAddress.
void ExpandAllowlistedNestedLeaves(std::vector<FieldInfo>& fields);

// Resolve Search/Drill field name against raw GetRawFields. Exact match first;
// else parse Parent.Child and synthesize an allowlisted leaf into `storage`.
// Returns pointer into `rawFields` or `&storage`; nullptr if unresolved.
const FieldInfo* ResolveFieldOrNestedLeaf(const std::vector<FieldInfo>& rawFields,
                                          const std::string& fieldName,
                                          FieldInfo& storage);
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
