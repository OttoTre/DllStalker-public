#pragma once

#include "pch.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "unity_dumper.h"

#include "gui/state/runtime/async_loader_set.h"
#include "gui/state/core/class_cache_model.h"
#include "gui/state/core/edit_buffer_store.h"
#include "gui/state/core/enum_literal_cache.h"
#include "gui/state/core/image_cache_model.h"
#include "gui/state/fields/field_snapshot_model.h"
#include "gui/state/fields/field_watch_model.h"
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
    State::InvokeRequestQueue  invokeQueue{};
    State::InspectorHistoryModel         history{};
    State::InspectorBookmarksModel       bookmarks{};
    State::FieldSnapshotModel            fieldSnapshot{};
    State::FieldWatchModel               fieldWatch{};
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
    bool   fieldsAutoRefresh         = false;
    int    fieldsRefreshIntervalIndex = 1;
    double fieldsLastRefreshAt        = 0.0;

    // App shell auto-selects imgSearchBuffer once after the image cache loads.
    // Cleared after one attempt; Refresh Images re-arms when nothing is selected.
    bool pendingDefaultImageSelection = true;

    std::string cachedLowerFilter{};
    std::string cachedOriginalFilter{};
    std::string methodsCachedOriginalFilter{};
    std::string methodsCachedLowerFilter{};
    std::string fieldsCachedOriginalFilter{};
    std::string fieldsCachedLowerFilter{};

    // ---- Worker bundle (declared after data so jthreads join cleanly) -----
    State::AsyncLoaderSet loaders{};

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
                             const Engine::FieldInfo& sourceField,
                             void* ownerKlass,
                             void* ownerInstance);

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
