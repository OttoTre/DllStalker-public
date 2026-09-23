#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

namespace Gui
{
struct ControlPanelSessionState;
}

namespace Gui::State
{
struct InspectorBookmarksModel;
struct FieldWatchModel;
struct NavigationSnapshot;

// Bookmark + watch *recipes* (names, not heap pointers) under
// <proxy>/stalker_runtime/session/. Sibling of mods/; never .lua.
struct SessionPersist {
    // Stamp persistable names/indices onto an in-RAM snapshot (never pointers).
    // When `state` is set and the root klass is live, refreshes `rootClassName`
    // from LookupClassDisplayName only. If lookup is empty, keep existing
    // `rootClassName` (never TryGetClassNameFromInstance).
    static void StampBookmarkNamedPath(NavigationSnapshot& snap,
                                       const ControlPanelSessionState* state = nullptr);
    static bool SaveBookmarks(const InspectorBookmarksModel& bookmarks);
    static bool SaveWatches(const FieldWatchModel& watches);
    static void LoadInto(Gui::ControlPanelSessionState& state);
    static void Rebind(Gui::ControlPanelSessionState& state);
};
} // namespace Gui::State

#endif // ENABLE_DUMPER
