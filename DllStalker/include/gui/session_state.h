#pragma once

#include "pch.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "unity_dumper.h"

#include "gui/state/async_loader_set.h"
#include "gui/state/class_cache_model.h"
#include "gui/state/edit_buffer_store.h"
#include "gui/state/image_cache_model.h"
#include "gui/state/field_snapshot_model.h"
#include "gui/state/field_watch_model.h"
#include "gui/state/call_log_model.h"
#include "gui/state/inspector_bookmarks_model.h"
#include "gui/state/inspector_history_model.h"
#include "gui/state/inspector_history_types.h"
#include "gui/state/inspector_navigation_feedback.h"
#include "gui/state/inspector_model.h"
#include "gui/state/invoke_request_queue.h"
#include "gui/state/walker_controller.h"

namespace Gui
{
// Facade over the composed sub-models that make up the Control Panel's
// session-wide state. Each model owns its data and synchronization; the
// facade orchestrates cross-model operations (async loads, navigation,
// invoke dispatch).
//
// Backward-compat: legacy field names are exposed as references to model
// fields so the GUI views compile untouched in this phase. Phase 6 will
// split the views and let those names retire.
//
// Member-order rules:
//   * `loaders` (AsyncLoaderSet, contains jthreads) is declared BEFORE all
//     reference aliases so those NSDMI bindings are well-formed and so
//     `loaders` is destroyed AFTER... wait, scratch that — destruction is
//     reverse declaration. We rely on jthread's destructor (request_stop +
//     join) to cleanly stop workers before the data they captured by
//     reference goes away. Trick: declare `loaders` AFTER all data (so
//     jthreads join first), then declare the (trivially destructible)
//     reference aliases at the very end. References don't really
//     "destruct" so destruction order is preserved correctly:
//       1. ref aliases (trivial no-op)
//       2. loaders (jthreads join here, while mutexes/data are still alive)
//       3. data members (caches, mutexes, models)
struct ControlPanelSessionState {
    // ---- Composed data models ---------------------------------------------
    std::shared_ptr<Engine::UnityDumper> dumper = nullptr;

    State::ImageCacheModel     imageCache{};
    State::ClassCacheModel     classCache{};
    State::InspectorModel      inspector{};
    State::EditBufferStore     editBufferStore{};
    State::WalkerController    walker{};
    State::InvokeRequestQueue  invokeQueue{};
    State::InspectorHistoryModel         history{};
    State::InspectorBookmarksModel       bookmarks{};
    State::FieldSnapshotModel            fieldSnapshot{};
    State::FieldWatchModel               fieldWatch{};
    State::CallLogModel                  callLog{};
    State::InspectorNavigationFeedback   navigationFeedback{};

    // ---- Plain UI state ----------------------------------------------------
    void* selectedImage = nullptr;
    void* selectedClass = nullptr;

    char imgSearchBuffer[128]    = "Assembly-CSharp";
    char imageFilterBuffer[128]  = "";
    char classFilterBuffer[128]  = "";
    char methodsFilterBuffer[128] = "";
    char fieldsFilterBuffer[128]  = "";
    int  selectedDumpMode        = 0;

    bool   fieldsAutoRefresh         = false;
    int    fieldsRefreshIntervalIndex = 1;
    double fieldsLastRefreshAt        = 0.0;

    // First-load reconciler: when true, the app shell tries to resolve
    // `imgSearchBuffer` against the loaded image cache once it arrives and
    // applies it via SelectImage(). Cleared after the first attempt (match
    // or miss) so a typo doesn't scan every frame. Re-armed by the Refresh
    // Images button when no image is currently selected.
    bool pendingDefaultImageSelection = true;

    std::string cachedLowerFilter{};
    std::string cachedOriginalFilter{};
    std::string methodsCachedOriginalFilter{};
    std::string methodsCachedLowerFilter{};
    std::string fieldsCachedOriginalFilter{};
    std::string fieldsCachedLowerFilter{};

    // ---- Worker bundle (declared after data so jthreads join cleanly) -----
    State::AsyncLoaderSet loaders{};

    // ---- Backward-compat reference aliases --------------------------------
    // Declared AFTER all real members so every reference NSDMI binds to an
    // already-constructed target. References themselves have trivial
    // destruction so they don't disturb the worker-join ordering above.
    std::vector<Engine::ImageInfo>& uiImageCache       = imageCache.data;
    std::mutex&                     imageCacheMutex    = imageCache.mutex;

    std::vector<Engine::ClassInfo>& uiClassCache       = classCache.data;
    std::mutex&                     classCacheMutex    = classCache.mutex;

    InspectorCache& inspectorCache              = inspector.cache;
    std::mutex&     inspectorCacheMutex         = inspector.mutex;
    int&            selectedInstanceIndex       = inspector.selectedInstanceIndex;
    int&            instanceSearchMode          = inspector.instanceSearchMode;
    std::vector<void*>& rootInstanceCandidates  = inspector.rootInstanceCandidates;

    std::unordered_map<uintptr_t, std::array<char, 64>>& editBuffers = editBufferStore.buffers;

    std::vector<InspectorBreadcrumb>& navigationStack = walker.stack;

    int&                                 pendingInvokeMethodIndex     = invokeQueue.pendingInvokeMethodIndex;
    std::vector<std::array<char, 64>>&   invokeArgBuffers             = invokeQueue.argBuffers;
    std::mutex&                          invokeResultMutex            = invokeQueue.mutex;
    Engine::InvokeResult&                latestInvokeResult           = invokeQueue.latestResult;
    std::string&                         latestInvokeMethodName       = invokeQueue.latestMethodName;
    std::string&                         latestInvokeMethodParameters = invokeQueue.latestMethodParameters;
    std::string&                         latestInvokeArgsDisplay      = invokeQueue.latestArgsDisplay;
    std::atomic<int>&                    latestInvokeResultVersion    = invokeQueue.latestVersion;
    float&                               latestInvokeResultAtSeconds  = invokeQueue.latestAtSeconds;

    std::atomic<bool>& imageLoadInProgress      = loaders.imageLoadInProgress;
    std::atomic<bool>& classLoadInProgress      = loaders.classLoadInProgress;
    std::atomic<bool>& inspectorLoadInProgress  = loaders.inspectorLoadInProgress;
    std::atomic<bool>& fieldsLoadInProgress     = loaders.fieldsLoadInProgress;
    std::atomic<bool>& instanceSearchInProgress = loaders.instanceSearchInProgress;

    std::jthread& imageLoadThread       = loaders.imageLoadThread;
    std::jthread& classLoadThread       = loaders.classLoadThread;
    std::jthread& inspectorLoadThread   = loaders.inspectorLoadThread;
    std::jthread& fieldsLoadThread      = loaders.fieldsLoadThread;
    std::jthread& instanceSearchThread  = loaders.instanceSearchThread;

    // ---- Cache reset helpers -----------------------------------------------
    void ClearImageCache();
    void ClearClassCache();
    void ClearInspectorCache();

    // ---- Async load entry points -------------------------------------------
    void StartImageLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef);
    void StartClassLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedImageSnapshot);
    void StartInspectorLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot);
    void StartFieldsLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot);
    void StartStaticInstanceSearch(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot);
    void StartLiveInstanceSearch(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot);
    void StartInspectorLoadAtInstance(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* klass, void* instance);
    void StartCollectionLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef,
                             const Engine::FieldInfo& sourceField);

    // ---- Recursive Memory Walker -------------------------------------------
    bool NavigateIntoPointer(uintptr_t fieldValueAddress, std::string fieldLabel);
    bool NavigateIntoCollection(const Engine::FieldInfo& field);
    void NavigateBackTo(size_t breadcrumbIndex);
    void ResetNavigationStack();
    void EnsureRootBreadcrumb();

    // ---- Method Invoker ----------------------------------------------------
    void EnqueueInvoke(const Engine::MethodInfo& method,
                       void* instance,
                       std::vector<std::string> args);

    // ---- Selection / snapshot accessors ------------------------------------
    void SelectInstanceByIndex(int index);
    std::vector<Engine::ImageInfo> GetImageCacheSnapshot() const;
    std::vector<Engine::ClassInfo> GetClassCacheSnapshot() const;
    InspectorCache GetInspectorSnapshot();

    // ---- Image selection ---------------------------------------------------
    // Shared by the manual combo click in image_picker.cpp and the
    // first-load reconciler in app_shell.cpp so both paths apply the same
    // side effects (cache clears, StartClassLoad, history row).
    void SelectImage(const Engine::ImageInfo& img, bool recordHistory = true);

    // ---- Inspector history -------------------------------------------------
    State::NavigationSnapshot CaptureNavigationSnapshot(const char* actionLabel) const;
    void RecordNavigationEvent(const char* actionLabel);
    void RecordMethodAudit(const State::MethodAuditPayload& audit);
    void RecordFieldAudit(const State::FieldAuditPayload& audit);
    State::HistoryRestoreResult TryApplyHistoryEntry(const State::HistoryEntry& entry);

    // Shared restore implementation used by both history and bookmarks.
    // Writes failure messages via navigationFeedback.MarkStatus(); on
    // success leaves the banner untouched so callers pick their own label.
    State::HistoryRestoreResult TryApplyNavigationSnapshot(const State::NavigationSnapshot& snap);

    // ---- Bookmarks ---------------------------------------------------------
    bool BookmarkCurrentView(const char* name);
    State::HistoryRestoreResult TryApplyBookmark(uint32_t bookmarkId);
};
} // namespace Gui
