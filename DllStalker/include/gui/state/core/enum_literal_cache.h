#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "types/dumper_types.h"

namespace Gui::State
{
// Lazy cache of enum literal name/value lists keyed by engine enum klass pointer.
// UI-thread only (no mutex). Cleared when inspector/image/class selection resets.
struct EnumLiteralCache {
    std::unordered_map<void*, std::vector<Engine::EnumLiteral>> byKlass{};
    std::unordered_set<uintptr_t>                               customModeKeys{};

    void Clear() {
        byKlass.clear();
        customModeKeys.clear();
    }
};
} // namespace Gui::State

#endif // ENABLE_DUMPER
