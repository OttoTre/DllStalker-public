#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/inspector/dispatch_status.h"
#include "gui/views/transform/transform_tab.h"
#include "gui/views/transform/transform_editor_panel.h"
#include "gui/views/transform/transform_sources_panel.h"

#include "gui/chrome/ui_theme.h"
#include "gui/state/transform/transform_model.h"
#include "services/main_thread_dispatcher.h"

#include "imgui.h"

namespace Gui::Views
{
void RenderTransformTab(ControlPanelSessionState& state, const InspectorCache& snapshot) {
    auto& model = state.transformModel;
    model.editsDirty = false;

    model.Discover(state, snapshot);
    model.TickLiveRefresh(state);

    const bool dispatchAvailable  = Engine::Services::MainThreadDispatcher::IsDispatchAvailable();
    const bool mainThreadCaptured = Engine::Services::MainThreadDispatcher::IsMainThreadCaptured();
    const bool dispatchReady      = dispatchAvailable && mainThreadCaptured;

    if (!snapshot.activeInstancePtr) {
        ImGui::TextDisabled("Select an instance to inspect transform sources.");
        return;
    }

    ImGui::Checkbox("Live", &model.liveRefresh);
    ImGui::SameLine();

    if (UiTheme::IconLockButton("##edit_lock",
                                model.editLocked,
                                model.editLocked ? "Unlock edits" : "Lock edits")) {
        model.editLocked = !model.editLocked;
    }

    ImGui::SameLine();
    const bool canRefresh = dispatchReady
        && model.selectedIndex >= 0
        && model.selectedIndex < static_cast<int>(model.sources.size());
    if (!canRefresh) {
        ImGui::BeginDisabled();
    }
    if (UiTheme::IconRefreshButton("##refresh_transform", "Refresh transform")) {
        model.InvalidateFetch();
        model.EnqueueFetch(state, model.selectedIndex);
    }
    if (!canRefresh) {
        ImGui::EndDisabled();
    }

    // Active power toggle — only when a fetched GameObject-capable source is selected.
    if (model.selectedIndex >= 0 && model.selectedIndex < static_cast<int>(model.sources.size())
        && model.fetched.load()) {
        const auto& sel = model.sources[static_cast<size_t>(model.selectedIndex)];
        bool showActive = false;
        bool activeSelf = false;
        {
            std::lock_guard<std::mutex> lock(model.cacheMutex);
            showActive = model.hasActiveSelf || sel.isGameObject
                || sel.kind == State::TransformSourceKind::ImplicitGameObject;
            activeSelf = model.activeSelf;
        }
        if (showActive) {
            ImGui::SameLine();
            const bool activeDisabled = model.editLocked || !dispatchReady;
            if (activeDisabled) {
                ImGui::BeginDisabled();
            }
            if (UiTheme::IconActiveButton(
                    "##active_self",
                    activeSelf,
                    activeSelf ? "Deactivate GameObject (activeSelf)"
                               : "Activate GameObject (activeSelf)")) {
                if (!activeDisabled) {
                    model.EnqueueApplyActive(state, model.selectedIndex, !activeSelf);
                }
            }
            if (activeDisabled) {
                ImGui::EndDisabled();
            }
        }
    }

    if (model.implicitPending.load()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(resolving transform / gameObject...)");
    }

    RenderDispatchStatus("Transform read/write");

    if (model.stale.load()) {
        ImGui::TextColored(UiTheme::Tokens().error, "Selected source pointer is stale or unreadable.");
    }

    ImGui::Separator();

    if (!ImGui::BeginChild("TransformBody", ImVec2(0, 0), false)) {
        ImGui::EndChild();
        return;
    }

    if (model.sources.empty()) {
        ImGui::TextDisabled("No transform sources on this instance.");
        ImGui::EndChild();
        return;
    }

    RenderTransformSourcesPanel(model);

    ImGui::Separator();

    if (model.selectedIndex < 0 || model.selectedIndex >= static_cast<int>(model.sources.size())) {
        ImGui::EndChild();
        return;
    }

    if (dispatchReady && !model.fetched.load() && !model.pendingFetch.load()) {
        model.EnqueueFetch(state, model.selectedIndex);
    }

    if (!dispatchReady) {
        ImGui::EndChild();
        return;
    }

    if (model.pendingFetch.load() && !model.fetched.load()) {
        ImGui::TextDisabled("Fetching transform data...");
        ImGui::EndChild();
        return;
    }

    if (!model.fetched.load()) {
        ImGui::EndChild();
        return;
    }

    RenderTransformEditorPanel(state, model);
    ImGui::EndChild();
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
