#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/session_state.h"

#include "gui/state/history_steady_time.h"
#include "gui/state/history_validation.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace Gui
{
namespace
{
std::string LookupImageName(const ControlPanelSessionState& state, void* imagePtr) {
    if (!imagePtr) {
        return {};
    }
    for (const auto& img : state.GetImageCacheSnapshot()) {
        if (img.imagePtr == imagePtr) {
            return img.name;
        }
    }
    return "<image>";
}

std::string LookupClassName(const ControlPanelSessionState& state, void* classPtr) {
    if (!classPtr) {
        return {};
    }
    for (const auto& cl : state.GetClassCacheSnapshot()) {
        if (cl.klassPtr == classPtr) {
            if (!cl.ns.empty()) {
                return cl.ns + "::" + cl.name;
            }
            return cl.name;
        }
    }
    return "<class>";
}

// Resolve a snapshot's display class name. The sidebar class cache only
// holds klass pointers that came from the picker; after drill-in, the
// breadcrumb top.klass is a runtime-resolved pointer that won't be there.
// Fall back to TryGetClassNameFromInstance so bookmark / summary labels
// show the concrete runtime type instead of the "<class>" placeholder.
std::string ResolveSnapshotClassName(const ControlPanelSessionState& state,
                                     void* classPtr,
                                     void* instancePtr) {
    const std::string cached = LookupClassName(state, classPtr);
    if (!cached.empty() && cached != "<class>") {
        return cached;
    }
    if (instancePtr && state.dumper) {
        const std::string runtime =
            state.dumper->TryGetClassNameFromInstance(instancePtr, nullptr);
        if (!runtime.empty()) {
            return runtime;
        }
    }
    return {};
}

void SetNavigationStatus(ControlPanelSessionState& state, const char* message) {
    state.navigationFeedback.MarkStatus(message, State::HistorySteadyNowSeconds());
}

std::string TrimName(const char* raw) {
    if (!raw) {
        return {};
    }
    std::string s(raw);
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

void AppendNavigationBreadcrumbSuffix(std::string& label, const State::NavigationSnapshot& snap) {
    if (!snap.breadcrumbs.empty()) {
        const auto& top = snap.breadcrumbs.back();
        if (!top.label.empty()) {
            label += " @ ";
            label += top.label;
        }
    }
}
} // namespace

State::NavigationSnapshot ControlPanelSessionState::CaptureNavigationSnapshot(const char* actionLabel) const {
    State::NavigationSnapshot snap{};
    snap.imagePtr = selectedImage;
    snap.imageName = LookupImageName(*this, selectedImage);
    snap.classPtr = selectedClass;
    snap.instanceIndex = selectedInstanceIndex;
    snap.breadcrumbs = navigationStack;

    if (!snap.breadcrumbs.empty()) {
        const auto& top = snap.breadcrumbs.back();
        snap.instancePtr = top.instance;
        snap.classPtr    = top.klass;
    }
    else {
        std::lock_guard<std::mutex> lock(inspectorCacheMutex);
        snap.instancePtr = inspectorCache.activeInstancePtr;
        if (snap.classPtr == nullptr) {
            snap.classPtr = inspectorCache.activeClassPtr;
        }
    }

    // Resolve className after the effective (classPtr, instancePtr) is
    // known so drill-in snapshots can fall back to the runtime class name
    // when the breadcrumb's klass pointer is not in the sidebar cache.
    snap.className = ResolveSnapshotClassName(*this, snap.classPtr, snap.instancePtr);

    if (!actionLabel || actionLabel[0] == '\0') {
        snap.summaryLabel = State::NavigationLocationLabel(snap);
    }
    else {
        snap.summaryLabel = actionLabel;
        AppendNavigationBreadcrumbSuffix(snap.summaryLabel, snap);
    }

    if (snap.breadcrumbs.empty() && snap.instancePtr && dumper) {
        const std::string runtimeName = dumper->TryGetClassNameFromInstance(snap.instancePtr, nullptr);
        if (!runtimeName.empty()) {
            snap.summaryLabel += " @ ";
            snap.summaryLabel += runtimeName;
        }
    }

    return snap;
}

void ControlPanelSessionState::RecordNavigationEvent(const char* actionLabel) {
    State::HistoryEntry entry{};
    entry.timestampSeconds = State::HistorySteadyNowSeconds();
    entry.payload = CaptureNavigationSnapshot(actionLabel);
    history.Append(std::move(entry));
}

void ControlPanelSessionState::RecordMethodAudit(const State::MethodAuditPayload& audit) {
    State::HistoryEntry entry{};
    entry.timestampSeconds = State::HistorySteadyNowSeconds();
    entry.payload = audit;
    history.Append(std::move(entry));
}

void ControlPanelSessionState::RecordFieldAudit(const State::FieldAuditPayload& audit) {
    State::HistoryEntry entry{};
    entry.timestampSeconds = State::HistorySteadyNowSeconds();
    entry.payload = audit;
    history.Append(std::move(entry));
}

State::HistoryRestoreResult ControlPanelSessionState::TryApplyNavigationSnapshot(
    const State::NavigationSnapshot& snap) {
    // Failure paths write the user-facing reason here; success paths leave
    // the status banner untouched so the caller (TryApplyHistoryEntry,
    // TryApplyBookmark, ...) can pick its own success label.
    if (!dumper) {
        SetNavigationStatus(*this, "Dumper not initialized.");
        return State::HistoryRestoreResult::DumperUnavailable;
    }

    if (snap.instancePtr) {
        void* probeKlass = nullptr;
        if (!State::HistoryValidation::ValidateInstance(snap.instancePtr, dumper, &probeKlass)) {
            SetNavigationStatus(*this,
                             "Instance no longer valid — likely collected or memory changed.");
            return State::HistoryRestoreResult::StaleInstance;
        }
    }

    if (!snap.breadcrumbs.empty()) {
        size_t failedIndex = 0;
        const auto crumbResult = State::HistoryValidation::ValidateBreadcrumbStack(
            snap.breadcrumbs, dumper, &failedIndex);
        if (crumbResult == State::BreadcrumbValidationResult::StaleInstance) {
            SetNavigationStatus(*this,
                             "Instance no longer valid — likely collected or memory changed.");
            return State::HistoryRestoreResult::StaleInstance;
        }
        if (crumbResult == State::BreadcrumbValidationResult::StaleBreadcrumb) {
            SetNavigationStatus(*this,
                             "Breadcrumb target is no longer valid — nested object may have been collected.");
            return State::HistoryRestoreResult::StaleBreadcrumb;
        }
    }

    // Image-only branch: no class chosen yet. Wipe class+inspector caches
    // so a stale class list from a previous image doesn't leak into the UI
    // while the new image's classes load.
    if (snap.imagePtr && snap.classPtr == nullptr) {
        selectedImage = snap.imagePtr;
        selectedClass = nullptr;
        ClearClassCache();
        ClearInspectorCache();
        StartClassLoad(dumper, snap.imagePtr);

        // Patch A: image-only restore clears the active instance, so the
        // async-root gate in inspector_frame.cpp must not see a leftover
        // pointer here.
        navigationFeedback.lastAsyncRecordedInstance = nullptr;
        return State::HistoryRestoreResult::Applied;
    }

    if (snap.imagePtr) {
        selectedImage = snap.imagePtr;
    }
    selectedClass = snap.classPtr;

    void* instanceForGate = nullptr;
    {
        std::lock_guard<std::mutex> lock(inspectorCacheMutex);
        editBuffers.clear();
        inspectorCache.activeClassPtr = snap.classPtr;
        inspectorCache.activeInstancePtr = snap.instancePtr;
        selectedInstanceIndex = snap.instanceIndex;

        if (snap.instanceIndex >= 0
            && snap.instanceIndex < static_cast<int>(inspectorCache.instanceCandidates.size())) {
            inspectorCache.activeInstancePtr = inspectorCache.instanceCandidates[snap.instanceIndex];
        }
        instanceForGate = inspectorCache.activeInstancePtr;
    }

    navigationStack = snap.breadcrumbs;

    if (navigationStack.empty()) {
        if (snap.classPtr && !snap.instancePtr) {
            StartInspectorLoad(dumper, snap.classPtr);
            StartStaticInstanceSearch(dumper, snap.classPtr);
            // Patch A: instance pointer is null, async search is about to
            // run; reset the gate so the next found instance is recorded
            // exactly once (and not as a duplicate).
            navigationFeedback.lastAsyncRecordedInstance = nullptr;
            return State::HistoryRestoreResult::Applied;
        }
        if (snap.classPtr && snap.instancePtr) {
            StartInspectorLoadAtInstance(dumper, snap.classPtr, snap.instancePtr);
            // Patch A: prevent the async-root gate in inspector_frame.cpp
            // from logging a spurious "Instance search result" entry one
            // frame after restore (selectedInstanceIndex may be -1).
            navigationFeedback.lastAsyncRecordedInstance = instanceForGate;
            return State::HistoryRestoreResult::Applied;
        }
        navigationFeedback.lastAsyncRecordedInstance = nullptr;
        return State::HistoryRestoreResult::Applied;
    }

    const InspectorBreadcrumb& top = navigationStack.back();
    selectedClass = top.klass;
    if (top.isCollection) {
        StartCollectionLoad(dumper, top.sourceField);
    }
    else {
        StartInspectorLoadAtInstance(dumper, top.klass, top.instance);
    }

    // Patch A: drilled-into-breadcrumb path — same gate concern applies.
    navigationFeedback.lastAsyncRecordedInstance = instanceForGate;
    return State::HistoryRestoreResult::Applied;
}

State::HistoryRestoreResult ControlPanelSessionState::TryApplyHistoryEntry(const State::HistoryEntry& entry) {
    if (State::EntryKind(entry) != State::HistoryEntryKind::Navigation) {
        SetNavigationStatus(*this, "This entry is audit-only and cannot be restored.");
        return State::HistoryRestoreResult::InvalidEntryKind;
    }

    const auto* snap = std::get_if<State::NavigationSnapshot>(&entry.payload);
    if (!snap) {
        SetNavigationStatus(*this, "Invalid history entry payload.");
        return State::HistoryRestoreResult::InvalidEntryKind;
    }

    const auto result = TryApplyNavigationSnapshot(*snap);
    if (result != State::HistoryRestoreResult::Applied) {
        return result;
    }

    // Patch B: success messages live with the caller. Pick the label that
    // matches the branch the snapshot took.
    if (snap->imagePtr && snap->classPtr == nullptr) {
        SetNavigationStatus(*this, "Restored image selection.");
    }
    else if (!snap->breadcrumbs.empty()) {
        SetNavigationStatus(*this, "Restored inspector state.");
    }
    else if (snap->classPtr && !snap->instancePtr) {
        SetNavigationStatus(*this, "Restored class selection.");
    }
    else if (snap->classPtr && snap->instancePtr) {
        SetNavigationStatus(*this, "Restored inspector state.");
    }
    else {
        SetNavigationStatus(*this, "Restored selection.");
    }
    return result;
}

bool ControlPanelSessionState::BookmarkCurrentView(const char* name) {
    const std::string trimmed = TrimName(name);
    if (trimmed.empty()) {
        return false;
    }

    State::Bookmark entry{};
    entry.name         = trimmed;
    entry.snapshot     = CaptureNavigationSnapshot("");
    entry.createdAtSec = State::HistorySteadyNowSeconds();
    return bookmarks.Add(std::move(entry));
}

State::HistoryRestoreResult ControlPanelSessionState::TryApplyBookmark(uint32_t bookmarkId) {
    const State::Bookmark* bm = bookmarks.Find(bookmarkId);
    if (!bm) {
        SetNavigationStatus(*this, "Bookmark no longer exists.");
        return State::HistoryRestoreResult::BookmarkNotFound;
    }

    // Copy the snapshot up front -- TryApplyNavigationSnapshot mutates
    // session state and we still want a stable name for the success
    // message even if Find() were to be invalidated by a future change.
    const std::string capturedName = bm->name;
    const State::NavigationSnapshot capturedSnap = bm->snapshot;

    const auto result = TryApplyNavigationSnapshot(capturedSnap);
    if (result != State::HistoryRestoreResult::Applied) {
        return result;
    }

    // Patch B: success label owned by the caller. Bookmark apply also
    // does NOT call RecordNavigationEvent() -- bookmarks are intentionally
    // a quiet alternative to history.
    std::string banner = "Restored bookmark: ";
    banner += capturedName;
    SetNavigationStatus(*this, banner.c_str());
    return result;
}
} // namespace Gui

#endif // ENABLE_DUMPER
