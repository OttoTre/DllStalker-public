#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/inspector_frame.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"
#include "gui/state/navigation/inspector_navigation_snapshot.h"
#include "gui/views/breadcrumb_bar.h"
#include "gui/views/analysis_tab.h"
#include "gui/views/class_label_lookup.h"
#include "gui/views/fields_tab.h"
#include "gui/views/methods_tab.h"
#include "gui/views/transform_tab.h"

#include "imgui.h"

#include <string>
#include <vector>

namespace Gui::Views
{
namespace
{
const Gui::State::Bookmark* FindMatchingBookmark(const ControlPanelSessionState& state,
                                                 const Gui::State::NavigationSnapshot& current)
{
    for (const auto& bm : state.bookmarks.bookmarks) {
        if (Gui::State::NavigationFingerprintsEqual(bm.snapshot, current)) {
            return &bm;
        }
    }
    return nullptr;
}

void RenderInspectorNavChrome(ControlPanelSessionState& state)
{
    const Gui::State::NavigationSnapshot current = state.CaptureNavigationSnapshot("");
    const Gui::State::Bookmark* match = FindMatchingBookmark(state, current);
    const bool bookmarked = match != nullptr;

    // Prefer root-of-walk / sidebar class for bookmark naming — not the nested klass.
    std::string fallbackName;
    if (!state.walker.stack.empty() && state.walker.stack.front().klass != nullptr) {
        fallbackName = LookupClassDisplayName(state, state.walker.stack.front().klass);
        if (fallbackName.empty() && !state.walker.stack.front().label.empty()) {
            fallbackName = state.walker.stack.front().label;
        }
    }
    if (fallbackName.empty()) {
        fallbackName = LookupClassDisplayName(state, state.selectedClass);
    }
    if (fallbackName.empty()) {
        fallbackName = "Bookmark";
    }

    if (UiTheme::IconStarButton("##inspector_bookmark", bookmarked,
                                bookmarked ? "Remove bookmark" : "Bookmark current view")) {
        if (bookmarked) {
            const uint32_t id = match->id;
            state.bookmarks.Remove(id);
        }
        else {
            const std::string name = Gui::State::NavigationLocationLabel(current);
            state.BookmarkCurrentView(name.empty() ? fallbackName.c_str() : name.c_str());
        }
    }

    if (!state.walker.stack.empty()) {
        RenderBreadcrumbBar(state, /*continueSameLine=*/true);
    }
    else {
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(UiTheme::Tokens().text_object, "%s", fallbackName.c_str());
    }
}
} // namespace

void RenderInspector(ControlPanelSessionState& state, CopyFeedbackState& copyFeedback) {
    ImGui::SeparatorText("Inspector");

    if (!state.selectedClass) {
        ImGui::TextUnformatted("Select a class from the left sidebar.");
        return;
    }

    InspectorCache inspectorSnapshot = state.GetInspectorSnapshot();
    const bool topIsCollection = !state.walker.stack.empty()
                                 && state.walker.stack.back().isCollection;
    if (!topIsCollection
        && !state.loaders.inspectorLoadInProgress.load()
        && inspectorSnapshot.activeClassPtr != state.selectedClass) {
        state.StartInspectorLoad(state.dumper, state.selectedClass);
        inspectorSnapshot = state.GetInspectorSnapshot();
    }

    // Seed the root breadcrumb when the candidate-search worker (called via
    // StartStaticInstanceSearch / StartLiveInstanceSearch) has finished and
    // populated activeInstancePtr without going through SelectInstanceByIndex.
    // No-op once a root breadcrumb already exists.
    state.EnsureRootBreadcrumb();

    {
        void* currentInstance = nullptr;
        {
            std::lock_guard<std::mutex> lock(state.inspector.mutex);
            currentInstance = state.inspector.cache.activeInstancePtr;
        }
        if (currentInstance != nullptr
            && currentInstance != state.navigationFeedback.lastAsyncRecordedInstance
            && state.inspector.selectedInstanceIndex < 0) {
            state.RecordNavigationEvent("Instance search result");
            state.navigationFeedback.lastAsyncRecordedInstance = currentInstance;
        }
        if (currentInstance == nullptr) {
            // Do not clear lastAsyncRecordedInstance while loads are in flight —
            // a transient null would undo the latch set during restore.
            const bool loadInFlight = state.loaders.inspectorLoadInProgress.load(std::memory_order_relaxed)
                                   || state.loaders.instanceSearchInProgress.load(std::memory_order_relaxed);
            if (!loadInFlight) {
                state.navigationFeedback.lastAsyncRecordedInstance = nullptr;
            }
        }
    }

    if (state.walker.stack.size() <= 1
        && state.selectedClass
        && state.dumper
        && !state.loaders.inspectorLoadInProgress.load()
        && !state.loaders.fieldsLoadInProgress.load()
        && !state.loaders.instanceSearchInProgress.load()) {
        void* fieldsTarget    = nullptr;
        void* fieldsLoadedFor = nullptr;
        {
            std::lock_guard<std::mutex> lock(state.inspector.mutex);
            fieldsTarget    = state.inspector.cache.activeInstancePtr;
            fieldsLoadedFor = state.inspector.cache.fieldsLoadedForInstance;
        }
        if (fieldsTarget != nullptr && fieldsTarget != fieldsLoadedFor) {
            state.StartFieldsLoad(state.dumper, state.selectedClass);
        }
    }

    RenderInspectorNavChrome(state);

    if (state.loaders.inspectorLoadInProgress.load()) {
        ImGui::TextDisabled("Loading inspector data...");
    }

    if (!UiTheme::BeginUnderlineTabBar("InspectorTabs")) {
        return;
    }

    if (ImGui::BeginTabItem("Fields")) {
        RenderFieldsTab(inspectorSnapshot, copyFeedback, state.loaders.inspectorLoadInProgress.load(), state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Methods")) {
        RenderMethodsTab(inspectorSnapshot, copyFeedback, state.loaders.inspectorLoadInProgress.load(), state);
        ImGui::EndTabItem();
    }

    ImGuiTabItemFlags transformTabFlags = ImGuiTabItemFlags_None;
    if (state.transformModel.pendingFocusInstance != nullptr) {
        transformTabFlags = ImGuiTabItemFlags_SetSelected;
    }
    if (ImGui::BeginTabItem("Transform", nullptr, transformTabFlags)) {
        RenderTransformTab(state, inspectorSnapshot);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Analysis")) {
        RenderAnalysisTab(state, inspectorSnapshot);
        ImGui::EndTabItem();
    }

    UiTheme::EndUnderlineTabBar();
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
