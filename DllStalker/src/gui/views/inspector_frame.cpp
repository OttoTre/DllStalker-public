#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/inspector_frame.h"

#include "gui/views/breadcrumb_bar.h"
#include "gui/views/console_tab.h"
#include "gui/views/fields_tab.h"
#include "gui/views/methods_tab.h"

#include "imgui.h"

namespace Gui::Views
{
void RenderInspector(ControlPanelSessionState& state, AppShell::CopyFeedbackState& copyFeedback) {
    ImGui::SeparatorText("Inspector");

    if (!state.selectedClass) {
        ImGui::TextUnformatted("Select a class from the left sidebar.");
        return;
    }

    InspectorCache inspectorSnapshot = state.GetInspectorSnapshot();
    if (!state.inspectorLoadInProgress.load() && inspectorSnapshot.activeClassPtr != state.selectedClass) {
        state.StartInspectorLoad(state.dumper, state.selectedClass);
        inspectorSnapshot = state.GetInspectorSnapshot();
    }

    // Seed the root breadcrumb when the candidate-search worker (called via
    // StartStaticInstanceSearch / StartLiveInstanceSearch) has finished and
    // populated activeInstancePtr without going through SelectInstanceByIndex.
    // Idempotent — no-op once a breadcrumb exists.
    state.EnsureRootBreadcrumb();

    {
        void* currentInstance = nullptr;
        {
            std::lock_guard<std::mutex> lock(state.inspectorCacheMutex);
            currentInstance = state.inspectorCache.activeInstancePtr;
        }
        if (currentInstance != nullptr
            && currentInstance != state.navigationFeedback.lastAsyncRecordedInstance
            && state.selectedInstanceIndex < 0) {
            state.RecordNavigationEvent("Instance search result");
            state.navigationFeedback.lastAsyncRecordedInstance = currentInstance;
        }
        if (currentInstance == nullptr) {
            // Do not clear the async-record gate while inspector or instance
            // workers are swapping inspectorCache — a transient null would undo
            // suppression set by history/bookmark restore (Patch A).
            const bool loadInFlight = state.inspectorLoadInProgress.load(std::memory_order_relaxed)
                                   || state.instanceSearchInProgress.load(std::memory_order_relaxed);
            if (!loadInFlight) {
                state.navigationFeedback.lastAsyncRecordedInstance = nullptr;
            }
        }
    }

    // Reconciler: Static/Live discovery auto-fills activeInstancePtr but
    // never re-runs GetRawFields with the instance, so the Fields tab's
    // green "Active: 0x..." status bar otherwise lies about the values
    // beneath it. When (a) we're at the navigation root, (b) no worker is
    // mid-flight, and (c) the cached fields slice was loaded against a
    // different instance than the one now considered active, kick off the
    // missing StartFieldsLoad. Idempotent: once it publishes,
    // fieldsLoadedForInstance == activeInstancePtr and this is a no-op.
    if (state.navigationStack.size() <= 1
        && state.selectedClass
        && state.dumper
        && !state.inspectorLoadInProgress.load()
        && !state.fieldsLoadInProgress.load()
        && !state.instanceSearchInProgress.load()) {
        void* fieldsTarget    = nullptr;
        void* fieldsLoadedFor = nullptr;
        {
            std::lock_guard<std::mutex> lock(state.inspectorCacheMutex);
            fieldsTarget    = state.inspectorCache.activeInstancePtr;
            fieldsLoadedFor = state.inspectorCache.fieldsLoadedForInstance;
        }
        if (fieldsTarget != nullptr && fieldsTarget != fieldsLoadedFor) {
            state.StartFieldsLoad(state.dumper, state.selectedClass);
        }
    }

    RenderBreadcrumbBar(state);

    if (state.inspectorLoadInProgress.load()) {
        ImGui::TextUnformatted("Loading inspector data...");
    }

    if (!ImGui::BeginTabBar("InspectorTabs")) {
        return;
    }

    if (ImGui::BeginTabItem("Methods")) {
        RenderMethodsTab(inspectorSnapshot, copyFeedback, state.inspectorLoadInProgress.load(), state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Fields")) {
        RenderFieldsTab(inspectorSnapshot, copyFeedback, state.inspectorLoadInProgress.load(), state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Console")) {
        RenderConsoleTab(state);
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
