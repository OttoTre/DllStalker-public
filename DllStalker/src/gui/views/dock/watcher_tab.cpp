#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/dock/watcher_tab.h"

#include "gui/session_state.h"
#include "gui/config.h"
#include "gui/views/dock/navigation_status_banner.h"

#include "gui/state/navigation/history_steady_time.h"

#include "imgui.h"

#include <cfloat>
#include <cstdio>
#include <string>
#include <vector>

namespace Gui::Views
{
namespace
{
std::string WatchEntryLabel(const Gui::State::WatchedField& entry) {
    if (!entry.className.empty() && entry.className != "<class>") {
        return entry.className + "::" + entry.fieldName;
    }
    return entry.fieldName;
}

void CollectPlottableIds(const std::vector<Gui::State::WatchedField>& entries,
                         std::vector<uint32_t>& outIds) {
    outIds.clear();
    outIds.reserve(entries.size());
    for (const auto& entry : entries) {
        if (entry.plotEnabled) {
            outIds.push_back(entry.id);
        }
    }
}

const Gui::State::WatchedField* FindSnapshotEntry(const std::vector<Gui::State::WatchedField>& entries,
                                                  uint32_t id) {
    for (const auto& entry : entries) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

void RenderSelectedPlot(const Gui::State::WatchedField& entry) {
    if (entry.stale) {
        ImGui::TextDisabled("Address stale — Jump to restore context.");
        ImGui::TextDisabled("Last: %s", entry.lastDisplay.c_str());
    }

    if (entry.plot.count < 2) {
        ImGui::TextDisabled("Waiting for samples...");
        return;
    }

    std::vector<float> ordered;
    entry.plot.CopyOrdered(ordered);

    float minV = 0.0f;
    float maxV = 0.0f;
    entry.plot.MinMax(minV, maxV);

    char rangeBuf[96] = {};
    snprintf(rangeBuf, sizeof(rangeBuf), "min %.4g  max %.4g", minV, maxV);
    ImGui::TextDisabled("%s | %s", rangeBuf, entry.lastDisplay.c_str());

    const float plotH = ImGui::GetContentRegionAvail().y;
    ImGui::PlotLines("##plot",
                     ordered.data(),
                     static_cast<int>(ordered.size()),
                     0,
                     nullptr,
                     FLT_MAX,
                     FLT_MAX,
                     ImVec2(-1.0f, plotH));
}

void RenderWatcherWatchlist(ControlPanelSessionState& state,
                            const std::vector<Gui::State::WatchedField>& entries) {
    if (!ImGui::BeginChild("WatcherWatchlist", ImVec2(0, 0), false)) {
        return;
    }

    if (ImGui::BeginTable("WatcherTable",
                          4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
                              | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 168.0f);
        ImGui::TableHeadersRow();

        for (const auto& entry : entries) {
            const uint32_t id = entry.id;
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(id));

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(WatchEntryLabel(entry).c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(entry.typeName.c_str());

            ImGui::TableSetColumnIndex(2);
            if (entry.stale) {
                ImGui::TextDisabled("%s", entry.lastDisplay.c_str());
            }
            else {
                ImGui::TextUnformatted(entry.lastDisplay.c_str());
            }

            ImGui::TableSetColumnIndex(3);
            if (entry.plotEnabled) {
                if (ImGui::SmallButton("Plot")) {
                    state.fieldWatch.SetSelectedPlotWatchId(id);
                    state.fieldWatch.focusChartsTab = true;
                }
                ImGui::SameLine();
            }
            if (ImGui::SmallButton("Jump")) {
                const auto result = state.TryApplyNavigationSnapshot(entry.restoreSnapshot);
                if (result == Gui::State::HistoryRestoreResult::Applied) {
                    std::string banner = "Restored watch: ";
                    banner += WatchEntryLabel(entry);
                    state.navigationFeedback.MarkStatus(banner.c_str(),
                                                        Gui::State::HistorySteadyNowSeconds());
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove##watcher")) {
                state.fieldWatch.Remove(id);
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
}

void RenderWatcherCharts(ControlPanelSessionState& state,
                         const std::vector<Gui::State::WatchedField>& entries) {
    std::vector<uint32_t> plottableIds;
    CollectPlottableIds(entries, plottableIds);

    if (plottableIds.empty()) {
        ImGui::TextUnformatted(
            "No plottable watches. Watch an int/float field with [W] on the Fields tab.");
        return;
    }

    state.fieldWatch.EnsureValidPlotSelection();

    int selectedComboIndex = 0;
    for (size_t i = 0; i < plottableIds.size(); ++i) {
        if (plottableIds[i] == state.fieldWatch.selectedPlotWatchId) {
            selectedComboIndex = static_cast<int>(i);
            break;
        }
    }

    const Gui::State::WatchedField* selectedEntry =
        FindSnapshotEntry(entries, state.fieldWatch.selectedPlotWatchId);
    const std::string comboPreview =
        (selectedEntry != nullptr) ? WatchEntryLabel(*selectedEntry) : "";

    if (ImGui::BeginCombo("Series", comboPreview.c_str())) {
        for (size_t i = 0; i < plottableIds.size(); ++i) {
            const Gui::State::WatchedField* entry = FindSnapshotEntry(entries, plottableIds[i]);
            if (!entry) {
                continue;
            }
            const bool isSelected = (static_cast<int>(i) == selectedComboIndex);
            if (ImGui::Selectable(WatchEntryLabel(*entry).c_str(), isSelected)) {
                state.fieldWatch.SetSelectedPlotWatchId(plottableIds[i]);
            }
        }
        ImGui::EndCombo();
    }

    const Gui::State::WatchedField* entry = FindSnapshotEntry(entries, state.fieldWatch.selectedPlotWatchId);
    if (!entry || !entry->plotEnabled) {
        ImGui::TextDisabled("Select a series to plot.");
        return;
    }

    if (!ImGui::BeginChild("WatcherChartPlot", ImVec2(0, 0), false)) {
        return;
    }
    RenderSelectedPlot(*entry);
    ImGui::EndChild();
}
} // namespace

void RenderWatcherTab(ControlPanelSessionState& state) {
    if (ImGui::Button("Clear all##watcher")) {
        state.fieldWatch.Clear();
    }

    const std::vector<Gui::State::WatchedField> entries = state.fieldWatch.SnapshotEntries();

    ImGui::SameLine();
    ImGui::TextDisabled("%zu / %zu watched",
                        entries.size(),
                        Gui::State::FieldWatchModel::kMaxEntries);

    RenderNavigationStatusBanner(state);

    if (entries.empty()) {
        ImGui::TextUnformatted(
            "No watched fields. Toggle [W] on a field row in the Fields tab.");
        return;
    }

    const bool focusCharts = state.fieldWatch.ConsumeFocusChartsTab();
    std::vector<uint32_t> plottableIds;
    CollectPlottableIds(entries, plottableIds);
    const size_t plottableCount = plottableIds.size();

    char chartsTabLabel[32] = "Charts";
    if (plottableCount > 0) {
        snprintf(chartsTabLabel, sizeof(chartsTabLabel), "Charts (%zu)", plottableCount);
    }

    if (!ImGui::BeginTabBar("WatcherInner")) {
        return;
    }

    if (ImGui::BeginTabItem("Watchlist")) {
        RenderWatcherWatchlist(state, entries);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem(chartsTabLabel, nullptr, focusCharts ? ImGuiTabItemFlags_SetSelected : 0)) {
        RenderWatcherCharts(state, entries);
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
