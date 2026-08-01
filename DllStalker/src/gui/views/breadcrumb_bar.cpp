#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/breadcrumb_bar.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"

#include "imgui.h"

namespace Gui::Views
{
namespace
{
// Frame-height crumb matching the bookmark star geometry.
bool CrumbButton(const char* id, const char* label, const ImVec4& textColor, bool clickable)
{
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const float height = ImGui::GetFrameHeight();
    const float width = textSize.x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();
    if (clickable && (hovered || active)) {
        const ImU32 bg = ImGui::ColorConvertFloat4ToU32(
            active ? ImVec4(UiTheme::Tokens().accent.x, UiTheme::Tokens().accent.y,
                            UiTheme::Tokens().accent.z, 0.35f)
                   : ImVec4(UiTheme::Tokens().accent.x, UiTheme::Tokens().accent.y,
                            UiTheme::Tokens().accent.z, 0.20f));
        draw->AddRectFilled(rmin, rmax, bg, ImGui::GetStyle().FrameRounding);
    }

    const ImVec2 pos(rmin.x + (rmax.x - rmin.x - textSize.x) * 0.5f,
                     rmin.y + (rmax.y - rmin.y - textSize.y) * 0.5f);
    draw->AddText(pos, ImGui::ColorConvertFloat4ToU32(textColor), label);
    return clickable && pressed;
}

void CrumbSeparator()
{
    const char* sep = ">";
    const ImVec2 textSize = ImGui::CalcTextSize(sep);
    const float height = ImGui::GetFrameHeight();
    ImGui::InvisibleButton("##crumb_sep", ImVec2(textSize.x + 4.0f, height));
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();
    const ImVec2 pos(rmin.x + (rmax.x - rmin.x - textSize.x) * 0.5f,
                     rmin.y + (rmax.y - rmin.y - textSize.y) * 0.5f);
    ImGui::GetWindowDrawList()->AddText(
        pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), sep);
}
} // namespace

// When continueSameLine is true, the first crumb stays on the current line
// (used after the inspector bookmark star).
void RenderBreadcrumbBar(ControlPanelSessionState& state, bool continueSameLine) {
    if (state.walker.stack.empty()) {
        return;
    }

    bool pendingNavigate = false;
    size_t pendingIndex = 0;

    for (size_t i = 0; i < state.walker.stack.size(); ++i) {
        const auto& step = state.walker.stack[i];
        const bool isCurrent = (i + 1 == state.walker.stack.size());
        const char* label = step.label.empty() ? "<?>" : step.label.c_str();

        if (i > 0 || continueSameLine) {
            ImGui::SameLine(0.0f, 4.0f);
        }
        if (i > 0) {
            ImGui::PushID(static_cast<int>(i) + 1000);
            CrumbSeparator();
            ImGui::PopID();
            ImGui::SameLine(0.0f, 4.0f);
        }

        ImGui::PushID(static_cast<int>(i));
        if (isCurrent) {
            CrumbButton("##crumb", label, UiTheme::Tokens().text_object, /*clickable=*/false);
        }
        else if (CrumbButton("##crumb", label, UiTheme::Tokens().text_disabled, /*clickable=*/true)) {
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
