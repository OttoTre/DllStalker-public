#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/inspector/analysis_tab.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"
#include "gui/state/fields/field_snapshot_model.h"
#include "gui/views/fields/field_analysis_toolbar.h"
#include "gui/views/fields/field_compare_modal.h"

#include "imgui.h"

namespace Gui::Views
{
namespace
{
void RenderSnapshotChangesPanel(ControlPanelSessionState& state,
                                const InspectorCache& inspectorSnapshot,
                                bool fieldsBusy) {
    if (fieldsBusy) {
        ImGui::TextDisabled("Waiting for fields to finish loading...");
        return;
    }

    if (!state.fieldSnapshot.HasBaseline()) {
        ImGui::TextDisabled("Take a snapshot first.");
        return;
    }

    if (!state.fieldSnapshot.showChanges) {
        ImGui::TextDisabled("Enable Show changes to list diffs vs snapshot.");
        return;
    }

    if (state.fieldSnapshot.lastChangedCount == 0) {
        ImGui::TextDisabled("No changes vs snapshot.");
        return;
    }

    if (!ImGui::BeginChild("SnapshotChangesPane", ImVec2(0, 0), false)) {
        ImGui::EndChild();
        return;
    }

    if (ImGui::BeginTable("SnapshotChangesTable", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                              | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable
                              | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupColumn("Baseline", ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.16f);
        ImGui::TableHeadersRow();

        for (const auto& field : inspectorSnapshot.fields) {
            const std::string key = State::FieldKeyFromInfo(field);
            const State::FieldDiffTint tint = state.fieldSnapshot.TintForField(key);
            if (tint == State::FieldDiffTint::None) {
                continue;
            }

            const auto baselineIt = state.fieldSnapshot.baseline.find(key);
            const char* baseline =
                baselineIt != state.fieldSnapshot.baseline.end()
                    ? (baselineIt->second.baselineDisplay.empty()
                           ? "-"
                           : baselineIt->second.baselineDisplay.c_str())
                    : "-";
            const char* live =
                field.valueDisplay.empty() ? "-" : field.valueDisplay.c_str();

            ImGui::TableNextRow();
            ImGui::TableSetBgColor(
                ImGuiTableBgTarget_RowBg0,
                static_cast<ImU32>(State::FieldDiffTintToColor(tint)));

            ImGui::TableSetColumnIndex(0);
            UiTheme::DrawColumnName(field.name.c_str());
            ImGui::TableSetColumnIndex(1);
            UiTheme::DrawColumnValue(baseline);
            ImGui::TableSetColumnIndex(2);
            UiTheme::DrawColumnValue(live);
            ImGui::TableSetColumnIndex(3);
            UiTheme::DrawColumnType(field.type.c_str());
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
}
} // namespace

void RenderAnalysisTab(ControlPanelSessionState& state, const InspectorCache& inspectorSnapshot) {
    const bool inCollectionView = !state.walker.stack.empty()
                               && state.walker.stack.back().isCollection;
    const bool fieldsBusy = state.loaders.fieldsLoadInProgress.load()
                         || state.loaders.inspectorLoadInProgress.load();

    ImGui::TextDisabled("Snapshot / diff fields, or compare two instances.");

    RenderFieldAnalysisToolbar(state, inspectorSnapshot, inCollectionView, fieldsBusy);

    if (state.fieldSnapshot.showChanges && state.fieldSnapshot.HasBaseline() && !fieldsBusy) {
        state.fieldSnapshot.RecomputeDiff(inspectorSnapshot.fields);
    }

    if (!UiTheme::BeginUnderlineTabBar("AnalysisSubTabs")) {
        return;
    }

    if (ImGui::BeginTabItem("Diff")) {
        RenderSnapshotChangesPanel(state, inspectorSnapshot, fieldsBusy);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Compare")) {
        RenderTwoInstanceComparePanel(state, inspectorSnapshot);
        ImGui::EndTabItem();
    }

    UiTheme::EndUnderlineTabBar();
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
