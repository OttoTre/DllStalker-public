#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/class_browser.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"
#include "gui/infra/search_filter.h"

#include "imgui.h"

#include <string>
#include <vector>

namespace Gui::Views
{
void RenderClassBrowser(ControlPanelSessionState& state) {
    ImGui::SeparatorText("Class Browser");
    UiTheme::ElevatedFilter("##class_filter", state.classFilterBuffer, sizeof(state.classFilterBuffer),
                            -1.0f, "Filter...");

    std::vector<Engine::ClassInfo> classCacheSnapshot = state.GetClassCacheSnapshot();

    if (state.loaders.classLoadInProgress.load()) {
        ImGui::TextUnformatted("Loading classes...");
    }

    if (ImGui::BeginChild("ScrollArea", ImVec2(0, 0), true)) {
        const char* currentFilter = state.classFilterBuffer;
        if (strcmp(state.cachedOriginalFilter.c_str(), currentFilter) != 0) {
            state.cachedOriginalFilter = currentFilter;
            state.cachedLowerFilter = Gui::Infra::SearchFilter::ToLowercase(currentFilter);
        }

        const bool filterIsEmpty = state.cachedLowerFilter.empty();

        std::vector<size_t> visibleIndices;
        visibleIndices.reserve(classCacheSnapshot.size());
        for (size_t i = 0; i < classCacheSnapshot.size(); ++i) {
            const auto& cl = classCacheSnapshot[i];
            if (filterIsEmpty || Gui::Infra::SearchFilter::ClassMatches(cl, state.cachedLowerFilter)) {
                visibleIndices.push_back(i);
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visibleIndices.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& cl = classCacheSnapshot[visibleIndices[static_cast<size_t>(row)]];
                const bool isSelected = (state.selectedClass == cl.klassPtr);

                std::string display = cl.ns.empty() ? cl.name : (cl.ns + "::" + cl.name);

                ImGui::PushID(cl.klassPtr);
                if (UiTheme::BrowseSelectable(display.c_str(), isSelected)) {
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
