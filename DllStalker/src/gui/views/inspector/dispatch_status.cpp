#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/inspector/dispatch_status.h"

#include "gui/chrome/ui_theme.h"
#include "services/main_thread_dispatcher.h"

#include "imgui.h"

#include <cstdio>

namespace Gui::Views
{
void RenderDispatchStatus(const char* featureLabel) {
    const bool dispatchAvailable  = Engine::Services::MainThreadDispatcher::IsDispatchAvailable();
    const bool mainThreadCaptured = Engine::Services::MainThreadDispatcher::IsMainThreadCaptured();

    if (!dispatchAvailable) {
        char msg[192] = {};
        std::snprintf(msg, sizeof(msg),
                      "%s disabled (runtime_invoke hook unavailable)",
                      featureLabel);
        UiTheme::DrawErrorText(msg);
    }
    else if (!mainThreadCaptured) {
        UiTheme::DrawWarningText(
            "Waiting for engine to make a managed call (main thread not yet captured)...");
    }
    else {
        ImGui::TextDisabled("%s main thread = 0x%lX",
                            featureLabel,
                            static_cast<unsigned long>(
                                Engine::Services::MainThreadDispatcher::GetMainThreadId()));
    }

    const uint32_t dropped = Engine::Services::MainThreadDispatcher::GetDroppedJobCount();
    if (dropped > 0) {
        char msg[128] = {};
        std::snprintf(msg, sizeof(msg),
                      "Dispatcher dropped %u queued job(s) (queue full)",
                      dropped);
        UiTheme::DrawWarningText(msg);
    }
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
