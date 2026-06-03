#pragma once

#include "pch.h"

#include <mutex>
#include <vector>

#include "types/dumper_types.h"

namespace Gui
{
// Snapshot of everything the Inspector tabs need to render: methods +
// fields + instance candidate list + the currently active class /
// instance pair. Lives behind InspectorModel::mutex and is never read or
// written without it.
struct InspectorCache {
    std::vector<Engine::MethodInfo> methods{};
    std::vector<Engine::FieldInfo>  fields{};
    std::vector<void*>              instanceCandidates{};
    void* activeClassPtr    = nullptr;
    void* activeInstancePtr = nullptr;
    // Instance pointer that `fields` was last loaded against. nullptr means
    // the rows reflect GetRawFields(klass, nullptr) -- static-only values.
    // Compared against activeInstancePtr to detect "discovery happened but
    // fields are still static" and trigger a one-shot StartFieldsLoad from
    // the inspector frame reconciler.
    void* fieldsLoadedForInstance = nullptr;
};
} // namespace Gui

namespace Gui::State
{
// Mutex-guarded inspector data and the per-frame UI state that tracks
// which candidate instance and which discovery mode is active. The
// asynchronous loaders write into `cache`; the GUI thread reads via
// Snapshot().
struct InspectorModel
{
    InspectorCache cache{};
    mutable std::mutex mutex{};

    int selectedInstanceIndex = -1;
    int instanceSearchMode    = 0; // 0 = Static, 1 = Live API

    // Last successful Find Instances result for the sidebar class. Not cleared
    // on drill/collection so Compare-two-instances can use A/B after navigation.
    std::vector<void*> rootInstanceCandidates{};

    void Clear();
    InspectorCache Snapshot();
};
} // namespace Gui::State
