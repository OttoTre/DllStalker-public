#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <memory>
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
    // Selection candidates for the *current inspector view* (combo on Fields).
    // May be a Find-Instances copy, a single drilled instance, or empty in a
    // collection view. Do NOT use this for Compare A/B — use
    // InspectorModel::rootInstanceCandidates / SnapshotRootInstanceCandidates.
    std::vector<void*>              instanceCandidates{};
    void* activeClassPtr    = nullptr;
    void* activeInstancePtr = nullptr;
    // Instance pointer that `fields` was last loaded against. nullptr means
    // the rows reflect GetRawFields(klass, nullptr) -- static-only values.
    // Compared against activeInstancePtr to detect "discovery happened but
    // fields are still static" and trigger a one-shot StartFieldsLoad from
    // the inspector frame reconciler.
    void* fieldsLoadedForInstance = nullptr;
    // True after StartInspectorLoad / StartInspectorLoadAtInstance publishes
    // a methods vector (even if empty). Distinguishes "not loaded yet" from
    // "class has zero methods" so the reconciler does not spin.
    bool methodsCatalogLoaded = false;
};
} // namespace Gui

namespace Gui::State
{
// Mutex-guarded inspector data and the per-frame UI state that tracks
// which candidate instance and which discovery mode is active. The
// asynchronous loaders write into `cache`; the GUI thread reads via
// SnapshotShared() (generation-stamped shared_ptr — deep-copy once per
// mutation, cheap thereafter).
//
// Dual candidate lists (intentional):
//   * cache.instanceCandidates — view selection (Fields combo / active pick).
//     Writers: Find Instances, StartInspectorLoadAtInstance, collection load.
//   * rootInstanceCandidates — last sidebar Find Instances (Static/Live) only.
//     Survives drill/collection so Compare can pick A/B at class root.
//     Cleared by Clear(), ClearInspectorCache, and history class restore.
//     Readers must Snapshot* under the model mutex (never hold a raw
//     reference across frames).
struct InspectorModel
{
    InspectorCache cache{};
    mutable std::mutex mutex{};

    int selectedInstanceIndex = -1;
    int instanceSearchMode    = 0; // 0 = Static, 1 = Live API

    // Authoritative Compare / "root find" list — see dual-list note above.
    // Cleared by Clear() / ClearInspectorCache / history restore (class apply).
    std::vector<void*> rootInstanceCandidates{};
    // Klass that rootInstanceCandidates were published for (Find Instances).
    void* rootInstanceClassPtr = nullptr;

    // Call under mutex after mutating `cache` so SnapshotShared republishes.
    void NoteCacheMutated() { ++generation; }

    void Clear();
    // Deep-copy once per generation; subsequent frames share the pointer.
    std::shared_ptr<const InspectorCache> SnapshotShared() const;

    // Atomically publish a Find Instances result into both lists + selection.
    void PublishFindInstancesResult(std::vector<void*> candidates, void* klass);

    // Locked copy of rootInstanceCandidates for Compare / UI.
    std::vector<void*> SnapshotRootInstanceCandidates() const;

    // Locked: roots + owning klass (for Value Search class-scope checks).
    std::pair<std::vector<void*>, void*> SnapshotRootInstancesWithClass() const;

private:
    uint64_t generation = 0;
    mutable uint64_t publishedGeneration = 0;
    mutable std::shared_ptr<const InspectorCache> publishedView{}; // null until first SnapshotShared
};
} // namespace Gui::State

#endif // ENABLE_DUMPER
