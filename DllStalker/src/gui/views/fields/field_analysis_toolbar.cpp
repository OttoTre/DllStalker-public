#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/fields/field_analysis_toolbar.h"

#include "gui/session_state.h"
#include "gui/state/fields/field_snapshot_model.h"
#include "gui/views/fields/field_compare_modal.h"

#include "imgui.h"

namespace Gui::Views
{
void RenderFieldAnalysisToolbar(ControlPanelSessionState& state,
                                const InspectorCache& inspectorSnapshot,
                                bool inCollectionView,
                                bool fieldsBusy) {
    const State::FieldAnalysisScope scope = State::BuildFieldAnalysisScope(state);
    state.fieldSnapshot.InvalidateIfScopeChanged(scope);

    const bool canSnapshot =
        !fieldsBusy && !inspectorSnapshot.fields.empty() && state.selectedClass != nullptr;

    if (!canSnapshot) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Snapshot state", ImVec2(130, 0))) {
        state.fieldSnapshot.CaptureBaseline(inspectorSnapshot.fields, scope);
    }
    if (!canSnapshot) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Wait for fields to finish loading, or load fields first.");
        }
    }

    ImGui::SameLine();
    if (!state.fieldSnapshot.HasBaseline()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Clear snapshot", ImVec2(120, 0))) {
        state.fieldSnapshot.ClearBaseline();
    }
    if (!state.fieldSnapshot.HasBaseline()) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (!state.fieldSnapshot.HasBaseline()) {
        ImGui::BeginDisabled();
    }
    ImGui::Checkbox("Show changes", &state.fieldSnapshot.showChanges);
    if (!state.fieldSnapshot.HasBaseline()) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Take a snapshot first.");
        }
    }

    ImGui::SameLine();
    const size_t rootCandidateCount = state.inspector.rootInstanceCandidates.size();
    const bool canCompareTwo =
        rootCandidateCount >= 2 && state.selectedClass != nullptr;
    if (!canCompareTwo) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Compare two instances...", ImVec2(200, 0))) {
        RequestOpenTwoInstanceCompareModal();
    }
    if (!canCompareTwo) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                "At class root, use Find Instances to discover at least two candidates.");
        }
    }
    else if (ImGui::IsItemHovered()) {
        if (inCollectionView) {
            ImGui::SetTooltip(
                "Opens compare modal. Use Current path with an element index for A vs B here; "
                "class-field table is disabled in collection view.");
        }
        else if (state.walker.stack.size() > 1) {
            ImGui::SetTooltip(
                "Class fields = sidebar class layout. Current path = value at your drill path.");
        }
    }

    ImGui::SameLine();
    if (state.fieldSnapshot.HasBaseline()) {
        if (!state.fieldSnapshot.snapshotTimeLabel.empty()) {
            ImGui::TextDisabled("Snapshot: %zu fields @ %s",
                                state.fieldSnapshot.snapshotFieldCount,
                                state.fieldSnapshot.snapshotTimeLabel.c_str());
        }
        else {
            ImGui::TextDisabled("Snapshot: %zu fields",
                                state.fieldSnapshot.snapshotFieldCount);
        }
        if (state.fieldSnapshot.showChanges) {
            ImGui::SameLine();
            ImGui::TextDisabled("| %zu changed", state.fieldSnapshot.lastChangedCount);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Counts all changed fields; filter may hide some rows.");
            }
        }
    }
    else {
        ImGui::TextDisabled("No snapshot");
    }
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
