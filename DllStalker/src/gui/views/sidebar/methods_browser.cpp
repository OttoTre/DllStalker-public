#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/sidebar/methods_browser.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"

#include "imgui.h"

#include <cfloat>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace Gui::Views
{
namespace
{
const std::vector<State::MethodSearchRow>& HitsOrEmpty(
    const std::shared_ptr<const std::vector<State::MethodSearchRow>>& hits) {
    static const std::vector<State::MethodSearchRow> kEmpty{};
    return hits ? *hits : kEmpty;
}

std::string ClassDisplay(const State::MethodSearchRow& hit) {
    return hit.classNs.empty() ? hit.className : (hit.classNs + "::" + hit.className);
}

void FormatHitLabel(char* label, size_t labelSize, const State::MethodSearchRow& hit) {
    const std::string classDisplay = ClassDisplay(hit);
    if (hit.isStatic && hit.paramCountKnown) {
        std::snprintf(label, labelSize, "%s::%s  static  %d args",
                      classDisplay.c_str(), hit.methodName.c_str(), hit.paramCount);
    }
    else if (hit.isStatic) {
        std::snprintf(label, labelSize, "%s::%s  static",
                      classDisplay.c_str(), hit.methodName.c_str());
    }
    else if (hit.paramCountKnown) {
        std::snprintf(label, labelSize, "%s::%s  %d args",
                      classDisplay.c_str(), hit.methodName.c_str(), hit.paramCount);
    }
    else {
        std::snprintf(label, labelSize, "%s::%s",
                      classDisplay.c_str(), hit.methodName.c_str());
    }
}
} // namespace

void RenderMethodsBrowser(ControlPanelSessionState& state) {
    if (!state.selectedImage) {
        ImGui::TextDisabled("Select an image");
        return;
    }

    const auto snap = state.methodSearch.TakeSnapshot();
    const bool busy = snap.inProgress
        || state.loaders.methodIndexInProgress.load(std::memory_order_relaxed);
    const bool canSearch = state.dumper && state.selectedImage;

    const float iconSz = ImGui::GetFrameHeight();
    const float iconGap = ImGui::GetStyle().ItemSpacing.x;
    const float filterW = ImGui::GetContentRegionAvail().x - iconSz - iconGap;
    UiTheme::ElevatedFilter("##method_search", state.methodSearchBuffer,
                            sizeof(state.methodSearchBuffer), filterW, "Filter...");
    ImGui::SameLine(0.0f, iconGap);

    if (busy) {
        if (UiTheme::IconStopButton("##method_idx_stop", "Stop", iconSz)) {
            state.CancelImageMethodIndex();
        }
    }
    else if (UiTheme::IconSearchButton("##method_idx_search", "Search", iconSz, canSearch)) {
        state.StartImageMethodIndex();
    }

    const auto classCacheSnapshot = state.GetClassCacheSnapshot();
    const bool indexValid = snap.hasIndex
        && snap.indexCachePtr == classCacheSnapshot.get()
        && snap.indexCacheCount == classCacheSnapshot->size()
        && snap.indexImage == state.selectedImage;
    const auto& hits = HitsOrEmpty(snap.hits);

    if (indexValid && snap.indexTruncated) {
        UiTheme::DrawWarningText("Index truncated (65536).");
    }
    if (indexValid && snap.hitsTruncated) {
        UiTheme::DrawWarningText("Hit limit reached (500).");
    }

    if (ImGui::BeginChild("MethodSearchHits", ImVec2(0, 0), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        static uint64_t s_labelHitsGeneration = ~uint64_t{0};
        static std::vector<std::string> s_hitLabels;
        static float s_contentMinWidth = 0.0f;

        if (!indexValid && !busy) {
            ImGui::TextDisabled("Search to index methods");
        }
        else if (indexValid && hits.empty() && !busy) {
            ImGui::TextDisabled("No hits");
        }
        else if (!hits.empty() && indexValid) {
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
                            std::lock_guard<std::mutex> lock(state.methodSearch.mutex);
                            state.methodSearch.selectedHitIndex = row;
                        }
                        state.selectedClass = hit.klassPtr;
                        state.fieldsLastRefreshAt = 0.0;
                        state.ClearInspectorCache();
                        state.StartInspectorLoad(state.dumper, state.selectedClass);
                        state.StartStaticInstanceSearch(state.dumper, state.selectedClass);
                        const std::string historyLabel = hit.classNs.empty()
                            ? "Select class: " + hit.className
                            : "Select class: " + hit.classNs + "::" + hit.className;
                        state.RecordNavigationEvent(historyLabel.c_str());
                        state.pendingFocusInspectorMethodsTab = true;
                    }
                    ImGui::PopID();
                }
            }
        }
    }
    ImGui::EndChild();
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
