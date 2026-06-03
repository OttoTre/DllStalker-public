#pragma once

#include "pch.h"

#ifdef ENABLE_DUMPER

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "types/dumper_types.h"

namespace Gui
{
struct ControlPanelSessionState;
} // namespace Gui

namespace Gui::State
{
enum class FieldDiffTint { None, Changed, Increased, Decreased };

struct FieldSnapshotEntry {
    std::string fieldKey{};
    std::string typeName{};
    std::string baselineDisplay{};
};

struct FieldAnalysisScope {
    void*       sidebarClassPtr = nullptr;
    void*       viewInstancePtr = nullptr;
    size_t      breadcrumbCount = 0;
    bool        inCollection    = false;
    std::string collectionFieldName{};

    // Mirrors NavigationFingerprintsEqual breadcrumb shape for drill/collection parity.
    std::vector<std::string> breadcrumbLabels{};
    std::vector<bool>        breadcrumbIsCollection{};

    bool operator==(const FieldAnalysisScope& other) const;
    bool operator!=(const FieldAnalysisScope& other) const { return !(*this == other); }
};

struct FieldSnapshotModel {
    FieldAnalysisScope capturedScope{};
    std::unordered_map<std::string, FieldSnapshotEntry> baseline{};
    std::unordered_map<std::string, FieldDiffTint>      rowTints{};
    bool        showChanges     = false;
    size_t      lastChangedCount = 0;
    size_t      snapshotFieldCount = 0;
    std::string snapshotTimeLabel{};

    bool HasBaseline() const { return !baseline.empty(); }

    void InvalidateIfScopeChanged(const FieldAnalysisScope& scope);
    void CaptureBaseline(const std::vector<Engine::FieldInfo>& fields, const FieldAnalysisScope& scope);
    void RecomputeDiff(const std::vector<Engine::FieldInfo>& liveFields);
    void ClearBaseline();

    FieldDiffTint TintForField(const std::string& fieldKey) const;
};

FieldAnalysisScope BuildFieldAnalysisScope(const ControlPanelSessionState& state);

// ARGB color for ImGui::TableSetBgColor (cast to ImU32 at the view layer).
uint32_t FieldDiffTintToColor(FieldDiffTint tint);

FieldDiffTint DiffFieldDisplay(const std::string& typeName,
                               const std::string& baselineDisplay,
                               const std::string& liveDisplay);

std::string FieldKeyFromInfo(const Engine::FieldInfo& field);
} // namespace Gui::State

#endif // ENABLE_DUMPER
