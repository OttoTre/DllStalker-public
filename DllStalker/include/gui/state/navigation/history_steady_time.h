#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <chrono>

namespace Gui::State
{
// Single clock domain for inspector history timing.
//
// Used by both:
//   * HistoryEntry::timestampSeconds (set when an event is recorded)
//   * InspectorNavigationFeedback::statusUpdatedAtSec (set by MarkStatus)
//
// Returns seconds since the std::chrono::steady_clock epoch. Monotonic;
// safe for deltas; NOT a wall-clock value. Do not mix with ImGui::GetTime()
// or Win32 timers in comparisons -- read both sides through this helper.
inline double HistorySteadyNowSeconds() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
