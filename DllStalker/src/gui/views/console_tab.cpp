#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/console_tab.h"

#include "imgui.h"

namespace Gui::Views
{
void RenderConsoleTab(ControlPanelSessionState& state) {
    ImGui::TextUnformatted("Console integration planned in next phase.");
    ImGui::Separator();
    const char* modes[] = { "Dump Methods", "Dump Fields" };
    ImGui::Combo("Task", &state.selectedDumpMode, modes, IM_ARRAYSIZE(modes));
    if (ImGui::Button("Execute Task", ImVec2(-1, 30))) {
        if (!state.dumper) {
            ImGui::OpenPopup("NoDumperError");
        }
        else {
            try {
                switch (state.selectedDumpMode) {
                case 0: state.dumper->DumpMethods(state.selectedClass); break;
                case 1: state.dumper->DumpFields(state.selectedClass); break;
                }
            }
            catch (const std::exception&) {
                ImGui::OpenPopup("TaskExecutionError");
            }
            catch (...) {
                ImGui::OpenPopup("TaskExecutionError");
            }
        }
    }
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
