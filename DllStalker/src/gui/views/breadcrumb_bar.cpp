#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/breadcrumb_bar.h"

#include "imgui.h"

namespace Gui::Views
{
// Renders the navigation breadcrumb bar. The root entry (index 0) is the
// instance the user picked from the candidates combobox; subsequent entries
// are nested objects reached by clicking pointer-typed fields. Each entry
// except the last is a SmallButton that pops back to that level; the last
// entry is rendered as plain text since it is the current view.
void RenderBreadcrumbBar(ControlPanelSessionState& state) {
    if (state.navigationStack.empty()) {
        return;
    }

    // Defer the back-jump until after the loop to avoid mutating the stack
    // mid-iteration (NavigateBackTo can re-truncate based on liveness).
    bool pendingNavigate = false;
    size_t pendingIndex = 0;

    for (size_t i = 0; i < state.navigationStack.size(); ++i) {
        const auto& step = state.navigationStack[i];
        const bool isCurrent = (i + 1 == state.navigationStack.size());
        const char* label = step.label.empty() ? "<?>" : step.label.c_str();

        if (i > 0) {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextDisabled(">");
            ImGui::SameLine(0.0f, 4.0f);
        }

        ImGui::PushID(static_cast<int>(i));
        if (isCurrent) {
            // Current location: not a back-target. Render as bold-ish plain
            // text so the row reads "Player > m_Stats > [m_Base]" with the
            // last token visually anchored.
            ImGui::TextColored(ImVec4(0.95f, 0.95f, 0.65f, 1.0f), "%s", label);
        }
        else if (ImGui::SmallButton(label)) {
            pendingNavigate = true;
            pendingIndex = i;
        }
        ImGui::PopID();
    }

    if (pendingNavigate) {
        state.NavigateBackTo(pendingIndex);
    }
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
