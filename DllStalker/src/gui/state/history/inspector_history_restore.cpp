#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/session_state.h"

#include "gui/state/navigation/history_steady_time.h"
#include "gui/state/navigation/history_validation.h"
#include "gui/views/class_label_lookup.h"

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
    const std::string label = Views::LookupClassDisplayName(state, classPtr);
    return label.empty() ? std::string("<class>") : label;
}

// Resolve display class name for snapshots. Sidebar cache misses after drill-in;
// fall back to TryGetClassNameFromInstance for bookmark/history labels.
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
    snap.instanceIndex = inspector.selectedInstanceIndex;
    snap.breadcrumbs = walker.stack;

    if (!snap.breadcrumbs.empty()) {
        const auto& top = snap.breadcrumbs.back();
        snap.instancePtr = top.instance;
        snap.classPtr    = top.klass;
    }
    else {
        std::lock_guard<std::mutex> lock(inspector.mutex);
        snap.instancePtr = inspector.cache.activeInstancePtr;
        if (snap.classPtr == nullptr) {
            snap.classPtr = inspector.cache.activeClassPtr;
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
    // Failure sets the status banner; success leaves it for the caller.
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

        // No active instance — clear the async-instance history gate
        // (see lastAsyncRecordedInstance in inspector_frame.cpp).
        navigationFeedback.lastAsyncRecordedInstance = nullptr;
        return State::HistoryRestoreResult::Applied;
    }

    if (snap.imagePtr) {
        selectedImage = snap.imagePtr;
    }
    selectedClass = snap.classPtr;

    void* instanceForGate = nullptr;
    {
        std::lock_guard<std::mutex> lock(inspector.mutex);
        editBufferStore.buffers.clear();
        enumLiteralCache.Clear();
        inspector.cache.activeClassPtr = snap.classPtr;
        inspector.cache.activeInstancePtr = snap.instancePtr;
        inspector.selectedInstanceIndex = snap.instanceIndex;

        if (snap.instanceIndex >= 0
            && snap.instanceIndex < static_cast<int>(inspector.cache.instanceCandidates.size())) {
            inspector.cache.activeInstancePtr = inspector.cache.instanceCandidates[snap.instanceIndex];
        }
        instanceForGate = inspector.cache.activeInstancePtr;
    }

    walker.stack = snap.breadcrumbs;

    if (walker.stack.empty()) {
        if (snap.classPtr && !snap.instancePtr) {
            StartInspectorLoad(dumper, snap.classPtr);
            StartStaticInstanceSearch(dumper, snap.classPtr);
            // Static search will populate activeInstancePtr; reset the gate so
            // inspector_frame records exactly one "Instance search result" row.
            navigationFeedback.lastAsyncRecordedInstance = nullptr;
            return State::HistoryRestoreResult::Applied;
        }
        if (snap.classPtr && snap.instancePtr) {
            StartInspectorLoadAtInstance(dumper, snap.classPtr, snap.instancePtr);
            // Latch the gate so restore does not look like a fresh async search
            // (selectedInstanceIndex may still be -1).
            navigationFeedback.lastAsyncRecordedInstance = instanceForGate;
            return State::HistoryRestoreResult::Applied;
        }
        navigationFeedback.lastAsyncRecordedInstance = nullptr;
        return State::HistoryRestoreResult::Applied;
    }

    const InspectorBreadcrumb& top = walker.stack.back();
    selectedClass = top.klass;
    if (top.isCollection) {
        StartCollectionLoad(dumper, top.sourceField, top.klass, top.instance);
    }
    else {
        StartInspectorLoadAtInstance(dumper, top.klass, top.instance);
    }

    // Breadcrumb restore: same lastAsyncRecordedInstance latch as above.
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

    // TryApplyNavigationSnapshot leaves success text to the caller — pick a
    // banner that matches the snapshot shape.
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

    // Copy before TryApplyNavigationSnapshot mutates session state.
    const std::string capturedName = bm->name;
    const State::NavigationSnapshot capturedSnap = bm->snapshot;

    const auto result = TryApplyNavigationSnapshot(capturedSnap);
    if (result != State::HistoryRestoreResult::Applied) {
        return result;
    }

    // Success banner owned here. Bookmarks do not call RecordNavigationEvent().
    std::string banner = "Restored bookmark: ";
    banner += capturedName;
    SetNavigationStatus(*this, banner.c_str());
    return result;
}
} // namespace Gui

#endif // ENABLE_DUMPER
