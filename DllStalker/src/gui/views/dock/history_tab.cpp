#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/dock/history_tab.h"

#include "gui/session_state.h"
#include "gui/views/dock/navigation_status_banner.h"

#include "gui/state/navigation/history_steady_time.h"

#include "imgui.h"

#include <string>

namespace Gui::Views
{
namespace
{
std::string FormatRelativeAge(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    char buf[32];
    if (seconds < 60.0) {
        snprintf(buf, sizeof(buf), "(%ds ago)", static_cast<int>(seconds));
    }
    else if (seconds < 3600.0) {
        snprintf(buf, sizeof(buf), "(%dm ago)", static_cast<int>(seconds / 60.0));
    }
    else {
        const int hours = static_cast<int>(seconds / 3600.0);
        snprintf(buf, sizeof(buf), "(%dh ago)", hours > 99 ? 99 : hours);
    }
    return buf;
}
} // namespace

void RenderHistoryTab(ControlPanelSessionState& state) {
    if (ImGui::Button("Clear history")) {
        state.history.Clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu / %zu entries", state.history.Size(), State::InspectorHistoryModel::kMaxEntries);

    RenderNavigationStatusBanner(state);

    if (state.history.entries.empty()) {
        ImGui::TextUnformatted("No history yet. Navigate the inspector to record events.");
        return;
    }

    if (!ImGui::BeginChild("HistoryList", ImVec2(0, 0), true)) {
        return;
    }

    const double now = State::HistorySteadyNowSeconds();
    int rowIndex = 0;
    for (const auto& entry : state.history.entries) {
        ImGui::PushID(rowIndex++);

        const std::string ageSuffix = FormatRelativeAge(now - entry.timestampSeconds);

        switch (State::EntryKind(entry)) {
        case State::HistoryEntryKind::Navigation: {
            const auto* nav = std::get_if<State::NavigationSnapshot>(&entry.payload);
            if (!nav) {
                break;
            }
            const std::string label = nav->summaryLabel.empty() ? "Navigation" : nav->summaryLabel;
            if (ImGui::Selectable(label.c_str())) {
                state.TryApplyHistoryEntry(entry);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Click to restore this inspector location.");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ageSuffix.c_str());
            break;
        }
        case State::HistoryEntryKind::MethodAudit: {
            const auto* audit = std::get_if<State::MethodAuditPayload>(&entry.payload);
            if (!audit) {
                break;
            }
            const char* outcome = audit->succeeded ? "OK" : "ERR";
            const std::string row = "Run: " + audit->methodName + audit->argsDisplay
                                    + " -> " + outcome;
            ImGui::Selectable(row.c_str(), false, ImGuiSelectableFlags_Disabled);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Audit only - not undo.");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ageSuffix.c_str());
            if (ImGui::CollapsingHeader("Details##audit")) {
                if (!audit->parameters.empty()) {
                    ImGui::TextDisabled("Sig: %s", audit->parameters.c_str());
                }
                ImGui::TextWrapped("%s", audit->succeeded ? audit->returnDisplay.c_str() : audit->error.c_str());
            }
            break;
        }
        case State::HistoryEntryKind::FieldAudit: {
            const auto* audit = std::get_if<State::FieldAuditPayload>(&entry.payload);
            if (!audit) {
                break;
            }
            const std::string row = "Set: " + audit->fieldName + " = " + audit->newValueDisplay
                                    + " (" + audit->fieldType + ")";
            ImGui::Selectable(row.c_str(), false, ImGuiSelectableFlags_Disabled);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Audit only - not undo.");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", ageSuffix.c_str());
            break;
        }
        default:
            break;
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
