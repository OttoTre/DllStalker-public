#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/dock/navigation_status_banner.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"
#include "gui/state/navigation/history_steady_time.h"

#include "imgui.h"

namespace Gui::Views
{
void RenderNavigationStatusBanner(const ControlPanelSessionState& state, bool sameLine) {
    const double now = State::HistorySteadyNowSeconds();
    if (!state.navigationFeedback.IsStatusFresh(now)) {
        return;
    }
    if (sameLine) {
        ImGui::SameLine();
    }
    const char* text = state.navigationFeedback.statusMessage;
    switch (state.navigationFeedback.statusKind) {
    case State::NavigationStatusKind::Success:
        UiTheme::DrawSuccessText(text);
        break;
    case State::NavigationStatusKind::Error:
        UiTheme::DrawErrorText(text);
        break;
    case State::NavigationStatusKind::Warning:
    default:
        UiTheme::DrawWarningText(text);
        break;
    }
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
