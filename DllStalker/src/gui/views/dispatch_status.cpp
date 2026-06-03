#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/dispatch_status.h"

#include "services/main_thread_dispatcher.h"

#include "imgui.h"

namespace Gui::Views
{
void RenderDispatchStatus(const char* featureLabel) {
    const bool dispatchAvailable  = Engine::Services::MainThreadDispatcher::IsDispatchAvailable();
    const bool mainThreadCaptured = Engine::Services::MainThreadDispatcher::IsMainThreadCaptured();

    if (!dispatchAvailable) {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                           "%s disabled (runtime_invoke hook unavailable)",
                           featureLabel);
    }
    else if (!mainThreadCaptured) {
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.25f, 1.0f),
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
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
                           "Dispatcher dropped %u queued job(s) (queue full)",
                           dropped);
    }
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
