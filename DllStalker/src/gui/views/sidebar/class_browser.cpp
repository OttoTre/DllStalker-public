#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/sidebar/class_browser.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"
#include "gui/infra/search_filter.h"

#include "imgui.h"

#include <cfloat>
#include <cstring>
#include <string>
#include <vector>

namespace Gui::Views
{
namespace
{
constexpr float kClassFilterDebounceSec = 0.3f;

void RebuildClassFilterVisibleIndices(
    ControlPanelSessionState& state,
    const std::shared_ptr<const std::vector<Engine::ClassInfo>>& classes)
{
    state.classFilterVisibleIndices.clear();
    state.classFilterVisibleIndices.reserve(classes->size());
    const bool filterIsEmpty = state.cachedLowerFilter.empty();
    for (size_t i = 0; i < classes->size(); ++i) {
        const auto& cl = (*classes)[i];
        if (filterIsEmpty || Gui::Infra::SearchFilter::ClassMatches(cl, state.cachedLowerFilter)) {
            state.classFilterVisibleIndices.push_back(i);
        }
    }
    state.classFilterVisibleCachePtr = classes.get();
    state.classFilterVisibleCacheCount = classes->size();
}

void ApplyClassFilterNow(
    ControlPanelSessionState& state,
    const char* buffer,
    const std::shared_ptr<const std::vector<Engine::ClassInfo>>& classes)
{
    state.cachedOriginalFilter = buffer;
    state.cachedLowerFilter = Gui::Infra::SearchFilter::ToLowercase(buffer);
    RebuildClassFilterVisibleIndices(state, classes);
}
} // namespace

void RenderClassBrowser(ControlPanelSessionState& state) {
    ImGui::SeparatorText("Class Browser");
    if (UiTheme::ElevatedFilter("##class_filter", state.classFilterBuffer,
                                sizeof(state.classFilterBuffer), -1.0f, "Filter...")) {
        state.classFilterLastEditAt = ImGui::GetTime();
    }

    const auto classCacheSnapshot = state.GetClassCacheSnapshot();

    if (state.loaders.classLoadInProgress.load()) {
        ImGui::TextUnformatted("Loading classes...");
    }

    if (ImGui::BeginChild("ScrollArea", ImVec2(0, 0), true,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        const char* buffer = state.classFilterBuffer;
        const bool bufferEmpty = buffer[0] == '\0';
        const bool pending =
            strcmp(state.cachedOriginalFilter.c_str(), buffer) != 0;
        const bool cacheChanged =
            state.classFilterVisibleCachePtr != classCacheSnapshot.get()
            || state.classFilterVisibleCacheCount != classCacheSnapshot->size();

        if (bufferEmpty) {
            if (pending || cacheChanged) {
                ApplyClassFilterNow(state, buffer, classCacheSnapshot);
            }
        }
        else if (pending
                 && (ImGui::GetTime() - state.classFilterLastEditAt) >= kClassFilterDebounceSec) {
            ApplyClassFilterNow(state, buffer, classCacheSnapshot);
        }
        else if (cacheChanged) {
            // Applied filter unchanged; class list identity changed (reload / image).
            RebuildClassFilterVisibleIndices(state, classCacheSnapshot);
        }

        const auto& visibleIndices = state.classFilterVisibleIndices;

        // Stable H-scroll: measure visible set when applied filter/cache identity changes.
        static std::string s_widthFilterKey;
        static size_t s_widthClassCount = 0;
        static float s_contentMinWidth = 0.0f;
        const std::string widthKey = state.cachedLowerFilter;
        if (widthKey != s_widthFilterKey || classCacheSnapshot->size() != s_widthClassCount) {
            s_widthFilterKey = widthKey;
            s_widthClassCount = classCacheSnapshot->size();
            s_contentMinWidth = 0.0f;
            ImFont* font = ImGui::GetFont();
            const float font_size = ImGui::GetFontSize();
            for (size_t idx : visibleIndices) {
                const auto& cl = (*classCacheSnapshot)[idx];
                const std::string display = cl.ns.empty() ? cl.name : (cl.ns + "::" + cl.name);
                const float w = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, display.c_str()).x;
                if (w > s_contentMinWidth) {
                    s_contentMinWidth = w;
                }
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visibleIndices.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& cl = (*classCacheSnapshot)[visibleIndices[static_cast<size_t>(row)]];
                const bool isSelected = (state.selectedClass == cl.klassPtr);

                std::string display = cl.ns.empty() ? cl.name : (cl.ns + "::" + cl.name);

                ImGui::PushID(cl.klassPtr);
                if (UiTheme::BrowseSelectable(display.c_str(), isSelected, 0.0f, s_contentMinWidth)) {
                    state.selectedClass = cl.klassPtr;
                    state.fieldsLastRefreshAt = 0.0;
                    state.ClearInspectorCache();
                    state.StartInspectorLoad(state.dumper, state.selectedClass);
                    state.StartStaticInstanceSearch(state.dumper, state.selectedClass);
                    const std::string historyLabel = cl.ns.empty()
                        ? "Select class: " + cl.name
                        : "Select class: " + cl.ns + "::" + cl.name;
                    state.RecordNavigationEvent(historyLabel.c_str());
                }
                ImGui::PopID();

                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }
        ImGui::EndChild();
    }
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
