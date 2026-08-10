#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/sidebar/search_browser.h"

#include "gui/chrome/ui_theme.h"
#include "gui/config.h"
#include "gui/session_state.h"
#include "gui/views/sidebar/class_browser.h"

#include "imgui.h"

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Gui::Views
{
namespace
{
const std::vector<Engine::ValueSearchHit>& HitsOrEmpty(
    const std::shared_ptr<const std::vector<Engine::ValueSearchHit>>& hits) {
    static const std::vector<Engine::ValueSearchHit> kEmpty{};
    return hits ? *hits : kEmpty;
}

void FormatHitLabel(char* label, size_t labelSize, const Engine::ValueSearchHit& hit) {
    std::snprintf(label, labelSize, "%s::%p::%s = %s",
                  hit.className.c_str(),
                  hit.instance,
                  hit.fieldName.c_str(),
                  hit.valueDisplay.c_str());
}
} // namespace

void RenderSearchBrowser(ControlPanelSessionState& state) {
    const auto snap = state.valueSearch.TakeSnapshot();
    const auto& hits = HitsOrEmpty(snap.hits);
    const bool busy = snap.inProgress
        || state.loaders.valueSearchInProgress.load(std::memory_order_relaxed);
    const bool hasBaselineHits = !hits.empty() || busy;

    if (!state.selectedImage && !hasBaselineHits) {
        ImGui::TextDisabled("Select an image (assembly) first.");
        return;
    }
    if (!state.selectedImage) {
        ImGui::TextDisabled("Select an image to run a new Search.");
    }

    // =/~ match mode, then filter.
    UiTheme::GhostFilterModeButton(
        "vs_name_mode", &state.valueSearch.nameMatchStrict,
        "Strict", "Fuzzy");
    ImGui::SameLine();
    UiTheme::ElevatedFilter("##vs_name", state.valueSearchNameBuffer,
                            sizeof(state.valueSearchNameBuffer), -1.0f, "Field name...");

    UiTheme::GhostFilterModeButton(
        "vs_value_mode", &state.valueSearch.valueMatchStrict,
        "Strict", "Fuzzy");
    ImGui::SameLine();
    UiTheme::ElevatedFilter("##vs_value", state.valueSearchValueBuffer,
                            sizeof(state.valueSearchValueBuffer), -1.0f, "Value...");

    const bool hasNameOrValue =
        state.valueSearchNameBuffer[0] != '\0' || state.valueSearchValueBuffer[0] != '\0';
    // Bool/Enum always on; Number/String/Ptr chips only narrow. Name/value alone is enough.
    const bool canSearch = !busy && state.dumper && state.selectedImage && hasNameOrValue;
    const bool canDrill = !busy && state.dumper
        && state.valueSearchValueBuffer[0] != '\0' && !hits.empty();

    // Type: [Number String Ptr] … [Search Drill Deep] (icons right-aligned).
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Type:");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(4.0f * Config::GUI_SCALE, ImGui::GetStyle().FramePadding.y));
    ImGui::SameLine(0.0f, 0.0f);
    UiTheme::ChipToggle("Number", &state.valueSearch.chipNumber);
    ImGui::SameLine(0.0f, 0.0f);
    UiTheme::ChipToggle("String", &state.valueSearch.chipString);
    ImGui::SameLine(0.0f, 0.0f);
    UiTheme::ChipToggle("Ptr", &state.valueSearch.chipPtr);
    ImGui::PopStyleVar();

    const float iconSz = ImGui::GetFrameHeight();
    const float iconGap = ImGui::GetStyle().ItemSpacing.x;
    const float iconsW = 3.0f * iconSz + 2.0f * iconGap;
    ImGui::SameLine(0.0f, 0.0f);
    {
        const float rightX = ImGui::GetWindowContentRegionMax().x - iconsW;
        if (ImGui::GetCursorPosX() < rightX) {
            ImGui::SetCursorPosX(rightX);
        }
    }

    if (busy) {
        if (UiTheme::IconStopButton("##vs_stop", "Stop")) {
            state.CancelValueSearch();
        }
    }
    else if (UiTheme::IconSearchButton("##vs_search", "Search", -1.0f, canSearch)) {
        state.StartValueSearch();
    }

    ImGui::SameLine();
    if (UiTheme::IconDrillButton("##vs_drill", "Narrow hits by value", -1.0f, canDrill)) {
        state.StartValueDrill();
    }
    ImGui::SameLine();
    if (UiTheme::IconDeepButton("##vs_deep", state.valueSearch.chipDeep,
                                "Search inside Array/List elements and one PTR hop")) {
        state.valueSearch.chipDeep = !state.valueSearch.chipDeep;
    }

    if (snap.statusMessage && !snap.statusMessage->empty()) {
        ImGui::TextDisabled("%s", snap.statusMessage->c_str());
    }
    if (snap.truncReason == Engine::ValueSearchTruncReason::HitCap) {
        UiTheme::DrawWarningText("Hit limit reached (500). Narrow filters or Drill.");
    }

    ImGui::Separator();
    if (ImGui::BeginChild("ValueSearchHits", ImVec2(0, 0), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        // Key labels on hitsGeneration (not ptr+size) — equal-count Publish can reuse addresses.
        static uint64_t s_labelHitsGeneration = ~uint64_t{0};
        static std::vector<std::string> s_hitLabels;
        static float s_contentMinWidth = 0.0f;

        if (hits.empty()) {
            if (s_labelHitsGeneration != snap.hitsGeneration) {
                s_labelHitsGeneration = snap.hitsGeneration;
                s_hitLabels.clear();
                s_contentMinWidth = 0.0f;
            }
            if (!busy) {
                ImGui::TextDisabled("No hits.");
            }
        }
        else {
            if (snap.hitsGeneration != s_labelHitsGeneration) {
                s_labelHitsGeneration = snap.hitsGeneration;
                s_hitLabels.resize(hits.size());
                s_contentMinWidth = 0.0f;
                ImFont* font = ImGui::GetFont();
                const float font_size = ImGui::GetFontSize();
                for (size_t i = 0; i < hits.size(); ++i) {
                    char label[384] = {};
                    FormatHitLabel(label, sizeof(label), hits[i]);
                    s_hitLabels[i] = label;
                    const float w = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, label).x;
                    if (w > s_contentMinWidth) {
                        s_contentMinWidth = w;
                    }
                }
            }

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(hits.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const auto& hit = hits[static_cast<size_t>(row)];
                    const char* label = (static_cast<size_t>(row) < s_hitLabels.size())
                        ? s_hitLabels[static_cast<size_t>(row)].c_str()
                        : "";
                    ImGui::PushID(row);
                    const bool selected = (snap.selectedHitIndex == row);
                    if (UiTheme::BrowseSelectable(label, selected, 0.0f, s_contentMinWidth)) {
                        {
                            std::lock_guard<std::mutex> lock(state.valueSearch.mutex);
                            state.valueSearch.selectedHitIndex = row;
                        }
                        if (state.dumper && hit.klass && hit.instance) {
                            state.NavigateToValueSearchHit(hit);
                        }
                    }
                    ImGui::PopID();
                }
            }
        }
    }
    ImGui::EndChild();
}

void RenderSidebarBrowser(ControlPanelSessionState& state) {
    if (!UiTheme::BeginUnderlineTabBar("SidebarBrowserMode")) {
        return;
    }
    if (ImGui::BeginTabItem("Classes")) {
        state.sidebarBrowserMode = 0;
        RenderClassBrowser(state);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Search")) {
        state.sidebarBrowserMode = 1;
        RenderSearchBrowser(state);
        ImGui::EndTabItem();
    }
    UiTheme::EndUnderlineTabBar();
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
