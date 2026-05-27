#pragma once

#include "pch.h"

#include <string>
#include <variant>

#include "gui/state/inspector_navigation_snapshot.h"

namespace Gui::State
{
// Variant index = HistoryEntryKind value. Kept in sync via static_assert
// in inspector_history_model.cpp. Use EntryKind(entry) to read.
enum class HistoryEntryKind { Navigation = 0, MethodAudit = 1, FieldAudit = 2 };

struct MethodAuditPayload {
    std::string methodName{};
    std::string parameters{};
    std::string argsDisplay{};
    bool        succeeded = false;
    std::string returnDisplay{};
    std::string error{};
};

struct FieldAuditPayload {
    std::string fieldName{};
    std::string fieldType{};
    std::string newValueDisplay{};
};

struct HistoryEntry {
    // Seconds in the same clock domain as InspectorNavigationFeedback::statusUpdatedAtSec.
    // Sampled via Gui::State::HistorySteadyNowSeconds() (steady_clock); monotonic,
    // safe for deltas, NOT a wall-clock time. Do not compare against ImGui::GetTime().
    double timestampSeconds = 0.0;
    std::variant<NavigationSnapshot, MethodAuditPayload, FieldAuditPayload> payload{};
};

inline HistoryEntryKind EntryKind(const HistoryEntry& e) {
    return static_cast<HistoryEntryKind>(e.payload.index());
}
} // namespace Gui::State
