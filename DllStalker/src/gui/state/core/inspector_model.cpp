#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/core/inspector_model.h"

namespace Gui::State
{
void InspectorModel::Clear() {
    std::lock_guard<std::mutex> lock(mutex);
    cache = {};
    selectedInstanceIndex = -1;
    rootInstanceCandidates.clear();
    rootInstanceClassPtr = nullptr;
    NoteCacheMutated();
}

std::shared_ptr<const InspectorCache> InspectorModel::SnapshotShared() const {
    std::lock_guard<std::mutex> lock(mutex);
    if (!publishedView || publishedGeneration != generation) {
        publishedView = std::make_shared<const InspectorCache>(cache);
        publishedGeneration = generation;
    }
    return publishedView;
}

void InspectorModel::PublishFindInstancesResult(std::vector<void*> candidates, void* klass) {
    std::lock_guard<std::mutex> lock(mutex);
    rootInstanceCandidates = candidates;
    rootInstanceClassPtr = klass;
    cache.instanceCandidates = std::move(candidates);
    cache.activeClassPtr = klass;
    cache.activeInstancePtr =
        cache.instanceCandidates.empty() ? nullptr : cache.instanceCandidates.front();
    selectedInstanceIndex = cache.instanceCandidates.empty() ? -1 : 0;
    NoteCacheMutated();
}

std::vector<void*> InspectorModel::SnapshotRootInstanceCandidates() const {
    std::lock_guard<std::mutex> lock(mutex);
    return rootInstanceCandidates;
}

std::pair<std::vector<void*>, void*> InspectorModel::SnapshotRootInstancesWithClass() const {
    std::lock_guard<std::mutex> lock(mutex);
    return { rootInstanceCandidates, rootInstanceClassPtr };
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
