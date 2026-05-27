#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/class_browser.h"

#include "gui/infra/search_filter.h"

#include "imgui.h"

#include <vector>

namespace Gui::Views
{
void RenderClassBrowser(ControlPanelSessionState& state) {
    ImGui::SeparatorText("Class Browser");
    ImGui::InputText("Filter Classes", state.classFilterBuffer, sizeof(state.classFilterBuffer));

    std::vector<Engine::ClassInfo> classCacheSnapshot = state.GetClassCacheSnapshot();

    if (state.classLoadInProgress.load()) {
        ImGui::TextUnformatted("Loading classes...");
    }

    if (ImGui::BeginChild("ScrollArea", ImVec2(0, 0), true)) {
        const char* currentFilter = state.classFilterBuffer;
        if (strcmp(state.cachedOriginalFilter.c_str(), currentFilter) != 0) {
            state.cachedOriginalFilter = currentFilter;
            state.cachedLowerFilter = Gui::Infra::SearchFilter::ToLowercase(currentFilter);
        }

        const bool filterIsEmpty = state.cachedLowerFilter.empty();

        for (const auto& cl : classCacheSnapshot) {
            const bool matchesFilter = filterIsEmpty || Gui::Infra::SearchFilter::ClassMatches(cl, state.cachedLowerFilter);
            if (!matchesFilter) {
                continue;
            }

            const bool isSelected = (state.selectedClass == cl.klassPtr);

            if (!cl.ns.empty()) {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 1.0f, 1.0f), "%s", cl.ns.c_str());
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "::");
                ImGui::SameLine();
            }

            ImGui::PushID(cl.klassPtr);
            if (ImGui::Selectable(cl.name.c_str(), isSelected, ImGuiSelectableFlags_AllowItemOverlap)) {
                state.selectedClass = cl.klassPtr;
                state.fieldsLastRefreshAt = 0.0;
                state.ClearInspectorCache();
                state.StartInspectorLoad(state.dumper, state.selectedClass);
                state.StartStaticInstanceSearch(state.dumper, state.selectedClass);
                const std::string historyLabel = cl.ns.empty() ? "Select class: " + cl.name : "Select class: " + cl.ns + "::" + cl.name;
                state.RecordNavigationEvent(historyLabel.c_str());
            }
            ImGui::PopID();

            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndChild();
    }
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
