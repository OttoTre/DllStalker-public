#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/fields/field_analysis_toolbar.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"
#include "gui/state/fields/field_snapshot_model.h"

#include "imgui.h"

namespace Gui::Views
{
void RenderFieldAnalysisToolbar(ControlPanelSessionState& state,
                                const InspectorCache& inspectorSnapshot,
                                bool /*inCollectionView*/,
                                bool fieldsBusy) {
    const State::FieldAnalysisScope scope = State::BuildFieldAnalysisScope(state);
    state.fieldSnapshot.InvalidateIfScopeChanged(scope);

    const bool canSnapshot =
        !fieldsBusy && !inspectorSnapshot.fields.empty() && state.selectedClass != nullptr;
    const bool hasBaseline = state.fieldSnapshot.HasBaseline();

    if (!canSnapshot) {
        ImGui::BeginDisabled();
    }
    if (UiTheme::IconSnapshotButton("##snapshot", "Snapshot field state", -1.0f, canSnapshot)) {
        state.fieldSnapshot.CaptureBaseline(inspectorSnapshot.fields, scope);
    }
    if (!canSnapshot) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Wait for fields to finish loading, or load fields first.");
        }
    }

    ImGui::SameLine();
    if (!hasBaseline) {
        ImGui::BeginDisabled();
    }
    if (UiTheme::IconTrashButton("##clear_snapshot", "Clear snapshot", -1.0f, hasBaseline)) {
        state.fieldSnapshot.ClearBaseline();
    }
    if (!hasBaseline) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (!hasBaseline) {
        ImGui::BeginDisabled();
    }
    UiTheme::ChipToggle("Show changes", &state.fieldSnapshot.showChanges);
    if (!hasBaseline) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Take a snapshot first.");
        }
    }

    ImGui::SameLine();
    if (hasBaseline) {
        if (!state.fieldSnapshot.snapshotTimeLabel.empty()) {
            ImGui::TextDisabled("Snapshot: %zu fields @ %s",
                                state.fieldSnapshot.snapshotFieldCount,
                                state.fieldSnapshot.snapshotTimeLabel.c_str());
        }
        else {
            ImGui::TextDisabled("Snapshot: %zu fields",
                                state.fieldSnapshot.snapshotFieldCount);
        }
        if (state.fieldSnapshot.showChanges && !fieldsBusy) {
            ImGui::SameLine();
            ImGui::TextDisabled("| %zu changed", state.fieldSnapshot.lastChangedCount);
        }
    }
    else {
        ImGui::TextDisabled("No snapshot");
    }
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
