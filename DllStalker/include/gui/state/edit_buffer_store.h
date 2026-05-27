#pragma once

#include "pch.h"

#include <array>
#include <cstdint>
#include <unordered_map>

namespace Gui::State
{
// Per-field input buffers for the Fields tab. UI-thread only (no mutex).
// Keyed by FieldInfo::valueAddress so a buffer can never bleed across
// unrelated rows; the cache-reset paths Clear() this whenever the active
// class / instance changes.
struct EditBufferStore
{
    std::unordered_map<uintptr_t, std::array<char, 64>> buffers{};

    void Clear() { buffers.clear(); }
};
} // namespace Gui::State
