#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/inspector_frame.h"

#include "gui/session_state.h"
#include "gui/views/breadcrumb_bar.h"
#include "gui/views/fields_tab.h"
#include "gui/views/methods_tab.h"
#include "gui/views/transform_tab.h"

#include "imgui.h"

namespace Gui::Views
{
void RenderInspector(ControlPanelSessionState& state, CopyFeedbackState& copyFeedback) {
    ImGui::SeparatorText("Inspector");

    if (!state.selectedClass) {
        ImGui::TextUnformatted("Select a class from the left sidebar.");
        return;
    }

    InspectorCache inspectorSnapshot = state.GetInspectorSnapshot();
    // Do not reload the inspector when the active breadcrumb is a collection
    // view. Collection loads intentionally write the owner klass/instance
    // into activeClassPtr/activeInstancePtr so this reconciler does not fire
    // a full class reload and overwrite the synthesised element rows.
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

    // Static/Live discovery sets activeInstancePtr but does not reload fields
    // for that instance. At the navigation root, with no workers running,
    // call StartFieldsLoad when fieldsLoadedForInstance != activeInstancePtr.
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

    RenderBreadcrumbBar(state);

    if (state.loaders.inspectorLoadInProgress.load()) {
        ImGui::TextUnformatted("Loading inspector data...");
    }

    if (!ImGui::BeginTabBar("InspectorTabs")) {
        return;
    }

    if (ImGui::BeginTabItem("Methods")) {
        RenderMethodsTab(inspectorSnapshot, copyFeedback, state.loaders.inspectorLoadInProgress.load(), state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Fields")) {
        RenderFieldsTab(inspectorSnapshot, copyFeedback, state.loaders.inspectorLoadInProgress.load(), state);
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

    ImGui::EndTabBar();
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
