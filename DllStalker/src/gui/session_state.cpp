#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/session_state.h"

namespace Gui
{
// ==== Cache reset helpers ============================================
void ControlPanelSessionState::ClearImageCache() {
    imageCache.Clear();
}

void ControlPanelSessionState::ClearClassCache() {
    classCache.Clear();
}

void ControlPanelSessionState::ClearInspectorCache() {
    {
        std::lock_guard<std::mutex> lock(inspectorCacheMutex);
        inspectorCache = {};
        selectedInstanceIndex = -1;
        editBuffers.clear();
    }
    methodsFilterBuffer[0] = '\0';
    fieldsFilterBuffer[0] = '\0';
    methodsCachedOriginalFilter.clear();
    methodsCachedLowerFilter.clear();
    fieldsCachedOriginalFilter.clear();
    fieldsCachedLowerFilter.clear();
    // navigationStack is UI-thread-only; reset it whenever the inspector
    // returns to "no active target" so a future Select / Navigate starts
    // from a clean breadcrumb history.
    ResetNavigationStack();
}

// ==== Snapshot accessors =============================================
std::vector<Engine::ImageInfo> ControlPanelSessionState::GetImageCacheSnapshot() const {
    return imageCache.Snapshot();
}

std::vector<Engine::ClassInfo> ControlPanelSessionState::GetClassCacheSnapshot() const {
    return classCache.Snapshot();
}

InspectorCache ControlPanelSessionState::GetInspectorSnapshot() {
    return inspector.Snapshot();
}
} // namespace Gui

#endif // ENABLE_DUMPER
