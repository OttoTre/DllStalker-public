#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/dock/navigation_status_banner.h"

#include "gui/session_state.h"
#include "gui/state/navigation/history_steady_time.h"

#include "imgui.h"

namespace Gui::Views
{
void RenderNavigationStatusBanner(const ControlPanelSessionState& state) {
    const double now = State::HistorySteadyNowSeconds();
    if (!state.navigationFeedback.IsStatusFresh(now)) {
        return;
    }
    ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.4f, 1.0f),
                       "%s", state.navigationFeedback.statusMessage);
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
