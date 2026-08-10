#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <array>
#include <cstdint>
#include <unordered_map>

namespace Gui::State
{
// Per-field input buffers for the Fields tab. UI-thread only (no mutex).
// Keyed by FieldInfo::valueAddress so a buffer can never bleed across
// unrelated rows. Cleared on the GUI thread from Start*Load / selection
// resets — never from async worker threads.
struct EditBufferStore
{
    std::unordered_map<uintptr_t, std::array<char, 256>> buffers{};

    void Clear() { buffers.clear(); }
};
} // namespace Gui::State

#endif // ENABLE_DUMPER
