#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstddef>
#include <string>
#include <vector>

namespace Engine
{
// Lean per-field descriptors for Value Search (no live values).
// Nested allowlisted Unity components use dotted name (Parent.Child); no
// separate path field — see value_search_nested_leaves.h.
// Deep Array/List interiors use wildcard-index patterns (container[].member)
// — see value_search_element_leaves.h.
// Follow PTR (Deep): nested patterns ptr.field / ptr.arr[].member;
// unresolved type klass → ptr.* sentinel — see value_search_ptr_follow.h.
struct ValueSearchFieldSchema {
    std::string name{};
    std::string type{};
    size_t      offset   = 0;
    bool        isStatic = false;
    bool        isEnum   = false;
    // True for Follow-PTR nested / sentinel schema rows (Deep gate).
    // Parent / allowlisted nested / collection-interior-only rows stay false.
    bool        isPtrFollow = false;
};

struct ValueSearchClassSchema {
    void*                              klass = nullptr;
    std::vector<ValueSearchFieldSchema> fields{};
};
} // namespace Engine

#endif // ENABLE_DUMPER
