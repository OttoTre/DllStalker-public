#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/session_state.h"

#include "gui/state/history/inspector_history_types.h"
#include "scripting/runtime/script_runtime.h"
#include "services/main_thread_dispatcher.h"

namespace Gui
{
void ControlPanelSessionState::ClearTransformJumpCache() {
    transformJumpByValueAddress.clear();
}

void ControlPanelSessionState::BeginShutdown() {
    if (shutdownBegun) {
        return;
    }
    shutdownBegun = true;

    CancelInspectorCacheWriters();
    CancelValueSearch();

    // Remaining async loaders not covered by CancelInspectorCacheWriters.
    loaders.imageLoadThread = {};
    loaders.classLoadThread = {};
    loaders.imageLoadInProgress.store(false, std::memory_order_relaxed);
    loaders.classLoadInProgress.store(false, std::memory_order_relaxed);

    scriptModel.StopActive(Scripting::ScriptStopReason::Shutdown);

    // Drop pending jobs / reject enqueue — do not drain via runtime_invoke here.
    Engine::Services::MainThreadDispatcher::BeginShutdown();
}

void ControlPanelSessionState::ClearImageCache() {
    imageCache.Clear();
    if (dumper) {
        dumper->ClearValueSearchSchemas();
    }
}

void ControlPanelSessionState::ClearClassCache() {
    classCache.Clear();
    cachedOriginalFilter.clear();
    cachedLowerFilter.clear();
    classFilterLastEditAt = 0.0;
    classFilterVisibleIndices.clear();
    classFilterVisibleCachePtr = nullptr;
    classFilterVisibleCacheCount = 0;
}

void ControlPanelSessionState::ClearInspectorCache() {
    CancelInspectorCacheWriters();
    CancelValueSearch();
    {
        std::lock_guard<std::mutex> lock(inspector.mutex);
        inspector.cache = {};
        inspector.selectedInstanceIndex = -1;
        inspector.rootInstanceCandidates.clear();
        inspector.rootInstanceClassPtr = nullptr;
        inspector.NoteCacheMutated();
        editBufferStore.Clear();
        enumLiteralCache.Clear();
    }
    ClearTransformJumpCache();
    methodsFilterBuffer[0] = '\0';
    fieldsFilterBuffer[0] = '\0';
    methodsCachedOriginalFilter.clear();
    methodsCachedLowerFilter.clear();
    fieldsCachedOriginalFilter.clear();
    fieldsCachedLowerFilter.clear();
    // walker.stack is UI-thread-only; reset it whenever the inspector
    // returns to "no active target" so a future Select / Navigate starts
    // from a clean breadcrumb history.
    ResetNavigationStack();
}

void ControlPanelSessionState::TickPresentSideEffects() {
    // Drain invoke audits on the GUI thread (history has no mutex).
    if (invokeQueue) {
        State::MethodAuditPayload audit{};
        bool haveAudit = false;
        {
            std::lock_guard<std::mutex> lock(invokeQueue->mutex);
            if (invokeQueue->pendingMethodAudit) {
                audit.methodName = invokeQueue->latestMethodName;
                audit.parameters = invokeQueue->latestMethodParameters;
                audit.argsDisplay = invokeQueue->latestArgsDisplay;
                audit.succeeded = invokeQueue->latestResult.succeeded;
                audit.returnDisplay = invokeQueue->latestResult.returnDisplay;
                audit.error = invokeQueue->latestResult.error;
                invokeQueue->pendingMethodAudit = false;
                haveAudit = true;
            }
        }
        if (haveAudit) {
            RecordMethodAudit(audit);
        }
    }

    // Inspector auto-loads / history latches (not in RenderInspector).
    if (!selectedClass || !dumper) {
        return;
    }

    // Lightweight locked reads — avoid deep-copying InspectorCache every tick.
    void* activeClassPtr = nullptr;
    bool methodsCatalogLoaded = false;
    void* currentInstance = nullptr;
    void* fieldsLoadedFor = nullptr;
    int selectedInstanceIndex = -1;
    {
        std::lock_guard<std::mutex> lock(inspector.mutex);
        activeClassPtr = inspector.cache.activeClassPtr;
        methodsCatalogLoaded = inspector.cache.methodsCatalogLoaded;
        currentInstance = inspector.cache.activeInstancePtr;
        fieldsLoadedFor = inspector.cache.fieldsLoadedForInstance;
        selectedInstanceIndex = inspector.selectedInstanceIndex;
    }

    const bool topIsCollection = !walker.stack.empty() && walker.stack.back().isCollection;
    if (!topIsCollection
        && !loaders.inspectorLoadInProgress.load()
        && !loaders.instanceSearchInProgress.load()
        && (activeClassPtr != selectedClass || !methodsCatalogLoaded)) {
        StartInspectorLoad(dumper, selectedClass);
    }

    EnsureRootBreadcrumb();

    if (currentInstance != nullptr
        && currentInstance != navigationFeedback.lastAsyncRecordedInstance
        && selectedInstanceIndex < 0) {
        RecordNavigationEvent("Instance search result");
        navigationFeedback.lastAsyncRecordedInstance = currentInstance;
    }
    if (currentInstance == nullptr) {
        const bool loadInFlight = loaders.inspectorLoadInProgress.load(std::memory_order_relaxed)
                               || loaders.instanceSearchInProgress.load(std::memory_order_relaxed);
        if (!loadInFlight) {
            navigationFeedback.lastAsyncRecordedInstance = nullptr;
        }
    }

    if (walker.stack.size() <= 1
        && !loaders.inspectorLoadInProgress.load()
        && !loaders.fieldsLoadInProgress.load()
        && !loaders.instanceSearchInProgress.load()) {
        if (currentInstance != nullptr && currentInstance != fieldsLoadedFor) {
            StartFieldsLoad(dumper, selectedClass);
        }
    }
}

// ==== Snapshot accessors =============================================
std::shared_ptr<const std::vector<Engine::ImageInfo>>
ControlPanelSessionState::GetImageCacheSnapshot() const {
    return imageCache.Snapshot();
}

std::shared_ptr<const std::vector<Engine::ClassInfo>>
ControlPanelSessionState::GetClassCacheSnapshot() const {
    return classCache.Snapshot();
}

std::shared_ptr<const InspectorCache> ControlPanelSessionState::GetInspectorSnapshot() {
    return inspector.SnapshotShared();
}
} // namespace Gui

#endif // ENABLE_DUMPER
