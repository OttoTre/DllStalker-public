#pragma once

#include "pch.h"

#include <cstdint>
#include <string>
#include <vector>

#include "gui/state/navigation/inspector_navigation_snapshot.h"

namespace Gui::State
{
// In-session bookmark = user-named navigation snapshot. Reuses
// NavigationSnapshot from inspector_navigation_snapshot.h so apply paths can be
// shared with InspectorHistoryModel via TryApplyNavigationSnapshot.
//
// `id` is a stable handle for the row's lifetime: PushID across rename
// and delete needs to stay correct even if the underlying vector resizes
// or reorders.
struct Bookmark {
    uint32_t           id            = 0;
    std::string        name{};
    NavigationSnapshot snapshot{};

    // Steady-clock seconds at save time. Useful for sort-by-date in v1.
    // NOT a wall-clock value -- meaningless across DLL reloads. See
    // bookmark debt notes if/when persistence ships.
    double createdAtSec = 0.0;
};

struct InspectorBookmarksModel {
    std::vector<Bookmark> bookmarks{};
    uint32_t              nextId = 1;

    // All mutators trim the name; empty / whitespace-only is rejected.
    bool Add(Bookmark entry);
    bool Remove(uint32_t id);
    bool Rename(uint32_t id, const char* newName);

    // Non-const for in-place rename buffers; callers should not retain
    // the returned pointer across container mutations.
    Bookmark*       Find(uint32_t id);
    const Bookmark* Find(uint32_t id) const;

    void   Clear();
    size_t Size() const { return bookmarks.size(); }
};
} // namespace Gui::State
