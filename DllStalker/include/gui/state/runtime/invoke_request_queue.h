#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "types/dumper_types.h"

namespace Gui::State
{
// Method Invoker UI state + the latest-result snapshot.
// Owned via shared_ptr on ControlPanelSessionState so dispatcher jobs can
// capture the queue sink without pinning the whole session (panel teardown).
//   * pendingInvokeMethodIndex / argBuffers — UI-thread only; populated
//     when the user opens the args popup and read while it's visible.
//   * latest* fields — written by the runtime_invoke detour worker on
//     the engine main thread, read by the GUI thread under mutex. The
//     atomic version lets the GUI cheaply detect "did anything change
//     since last frame?".
struct InvokeRequestQueue
{
    int                                pendingInvokeMethodIndex = -1;
    std::vector<std::array<char, 64>>  argBuffers{};

    std::mutex             mutex{};
    Engine::InvokeResult   latestResult{};
    std::string            latestMethodName{};
    std::string            latestMethodParameters{};
    std::string            latestArgsDisplay{};
    std::atomic<int>       latestVersion = 0;
    // GUI-thread drains this into history; set when a result is published
    // so Methods-tab visibility is not required to record the audit.
    bool                   pendingMethodAudit = false;
    // Same "never shown" convention as InspectorNavigationFeedback::kStatusNeverShown:
    // a value older than any real sample so toast freshness checks hide the
    // banner until the GUI thread stamps a real time. Stays float because the
    // toast in methods_tab.cpp compares this against ImGui::GetTime() (also
    // float seconds); do NOT widen to double / change clock without auditing
    // every reader of latestInvokeResultAtSeconds.
    float                  latestAtSeconds = -1000.0f;
};
} // namespace Gui::State

#endif // ENABLE_DUMPER
