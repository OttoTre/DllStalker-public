#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/error_modals.h"

#include "imgui.h"

namespace Gui::Views
{
void RenderErrorPopups() {
    if (ImGui::BeginPopupModal("NoDumperError", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Dumper not initialized.");
        if (ImGui::Button("OK##NoDumper", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("TaskExecutionError", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Error executing task. Check console output.");
        if (ImGui::Button("OK##TaskError", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
