#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "unity_dumper.h"

#include "gui/state/runtime/async_loader_set.h"
#include "gui/state/core/class_cache_model.h"
#include "gui/state/core/edit_buffer_store.h"
#include "gui/state/core/enum_literal_cache.h"
#include "gui/state/core/image_cache_model.h"
#include "gui/state/fields/field_snapshot_model.h"
#include "gui/state/fields/field_watch_model.h"
#include "gui/state/fields/value_search_model.h"
#include "gui/state/runtime/call_log_model.h"
#include "gui/state/history/inspector_bookmarks_model.h"
#include "gui/state/history/inspector_history_model.h"
#include "gui/state/history/inspector_history_types.h"
#include "gui/state/navigation/inspector_navigation_feedback.h"
#include "gui/state/core/inspector_model.h"
#include "gui/state/runtime/invoke_request_queue.h"
#include "gui/state/runtime/script_model.h"
#include "gui/state/transform/transform_model.h"
#include "gui/state/navigation/walker_controller.h"

namespace Gui
{
// Facade over the composed sub-models that make up the Control Panel's
// session-wide state. Each model owns its data and synchronization; the
// facade orchestrates cross-model operations (async loads, navigation,
// invoke dispatch).
//
// Member-order rules:
//   * `loaders` (AsyncLoaderSet, contains jthreads) is declared after all
//     captured data so worker threads join before caches, mutexes, and models
//     are destroyed.
//   * Destruction is reverse declaration order: loaders are destroyed first,
//     then the data models they captured by reference.
struct ControlPanelSessionState {
    // ---- Composed data models ---------------------------------------------
    std::shared_ptr<Engine::UnityDumper> dumper = nullptr;

    State::ImageCacheModel     imageCache{};
    State::ClassCacheModel     classCache{};
    State::InspectorModel      inspector{};
    State::EditBufferStore     editBufferStore{};
    State::EnumLiteralCache    enumLiteralCache{};
    State::WalkerController    walker{};
    // Heap-owned so MainThreadDispatcher invoke jobs can capture a shared_ptr
    // sink and finish safely after ControlPanelSessionState is destroyed.
    std::shared_ptr<State::InvokeRequestQueue> invokeQueue =
        std::make_shared<State::InvokeRequestQueue>();
    State::InspectorHistoryModel         history{};
    State::InspectorBookmarksModel       bookmarks{};
    State::FieldSnapshotModel            fieldSnapshot{};
    State::FieldWatchModel               fieldWatch{};
    State::ValueSearchModel              valueSearch{};
    State::CallLogModel                  callLog{};
    State::InspectorNavigationFeedback   navigationFeedback{};
    State::TransformModel                transformModel{};
    State::ScriptModel                   scriptModel{};

    // ---- Plain UI state ----------------------------------------------------
    void* selectedImage = nullptr;
    void* selectedClass = nullptr;

    char imgSearchBuffer[128]    = "Assembly-CSharp";
    char imageFilterBuffer[128]  = "";
    char classFilterBuffer[128]  = "";
    char methodsFilterBuffer[128] = "";
    char fieldsFilterBuffer[128]  = "";
    char valueSearchNameBuffer[128]  = "";
    char valueSearchValueBuffer[128] = "";
    // 0 = Classes, 1 = Search (sidebar browser mode).
    int  sidebarBrowserMode = 0;
    bool   fieldsAutoRefresh         = false;
    int    fieldsRefreshIntervalIndex = 1;
    double fieldsLastRefreshAt        = 0.0;

    // App shell auto-selects imgSearchBuffer once after the image cache loads.
    // Cleared after one attempt; Refresh Images re-arms when nothing is selected.
    bool pendingDefaultImageSelection = true;

    // Fields [T] probe cache: valueAddress → show Transform jump.
    // Cleared on inspector/fields refresh; not walked on every Present row.
    std::unordered_map<uintptr_t, bool> transformJumpByValueAddress{};

    std::string cachedLowerFilter{};
    std::string cachedOriginalFilter{};
    // Class Browser: applied filter is debounced; buffer stays live while typing.
    double classFilterLastEditAt = 0.0;
    std::vector<size_t> classFilterVisibleIndices{};
    const void* classFilterVisibleCachePtr = nullptr;
    size_t classFilterVisibleCacheCount = 0;
    std::string methodsCachedOriginalFilter{};
    std::string methodsCachedLowerFilter{};
    std::string fieldsCachedOriginalFilter{};
    std::string fieldsCachedLowerFilter{};

    // ---- Worker bundle (declared after data so jthreads join cleanly) -----
    State::AsyncLoaderSet loaders{};

    // BeginShutdown once-guard (GUI thread only).
    bool shutdownBegun = false;

    // ---- Present tick (GUI thread, before paint) ---------------------------
    // Auto-loads, history latches, and invoke-audit drain (not inside Render*).
    void TickPresentSideEffects();

    // ---- Teardown (GUI thread, before jthread joins in destructor) ---------
    // Best-effort cancel loaders/Search/scripts and reject MainThreadDispatcher
    // work. Idempotent; call once when leaving RunControlPanelMainLoop.
    void BeginShutdown();

    // ---- Cache reset helpers -----------------------------------------------
    void ClearImageCache();
    void ClearClassCache();
    void ClearInspectorCache();
    void ClearTransformJumpCache();

    // ---- Async load entry points -------------------------------------------
    void StartImageLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef);
    void StartClassLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedImageSnapshot);
    void StartInspectorLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot);
    void StartFieldsLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot);
    void StartStaticInstanceSearch(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot);
    void StartLiveInstanceSearch(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot);
    void StartInspectorLoadAtInstance(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* klass, void* instance);
    void StartCollectionLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef,
                             const Engine::FieldInfo& sourceField,
                             void* ownerKlass,
                             void* ownerInstance);
    // Cancel in-flight Analysis Compare worker (GUI-thread; joins prior jthread).
    void CancelInstanceCompare();
    // Join all workers that write inspector.cache / root candidates.
    void CancelInspectorCacheWriters();
    void CancelValueSearch();
    void StartValueSearch();
    void StartValueDrill();

    // ---- Recursive Memory Walker -------------------------------------------
    bool NavigateIntoPointer(uintptr_t fieldValueAddress, std::string fieldLabel);
    bool NavigateIntoCollection(const Engine::FieldInfo& field);
    // Search hit → walker path (collection / Deep / PTR-follow). Resets stack.
    // Unresolvable path falls back to load-at-root instance.
    bool NavigateToValueSearchHit(const Engine::ValueSearchHit& hit);
    void NavigateBackTo(size_t breadcrumbIndex);
    void ResetNavigationStack();
    void EnsureRootBreadcrumb();

    // ---- Method Invoker ----------------------------------------------------
    void EnqueueInvoke(const Engine::MethodInfo& method,
                       void* instance,
                       std::vector<std::string> args);

    // ---- Selection / snapshot accessors ------------------------------------
    void SelectInstanceByIndex(int index);
    std::shared_ptr<const std::vector<Engine::ImageInfo>> GetImageCacheSnapshot() const;
    std::shared_ptr<const std::vector<Engine::ClassInfo>> GetClassCacheSnapshot() const;
    std::shared_ptr<const InspectorCache> GetInspectorSnapshot();

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

#endif // ENABLE_DUMPER
