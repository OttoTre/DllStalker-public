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
}

InspectorCache InspectorModel::Snapshot() {
    std::lock_guard<std::mutex> lock(mutex);
    return cache;
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
