#pragma once

#include "pch.h"

#include <deque>

#include "gui/state/inspector_history_types.h"

namespace Gui::State
{
struct InspectorHistoryModel {
    static constexpr size_t kMaxEntries = 64;

    std::deque<HistoryEntry> entries{};

    void Append(HistoryEntry entry);
    void Clear();
    size_t Size() const { return entries.size(); }
};
} // namespace Gui::State
