#pragma once

#include "pch.h"

#ifdef ENABLE_DUMPER

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "gui/state/inspector_navigation_snapshot.h"
#include "types/dumper_types.h"

namespace Gui
{
struct ControlPanelSessionState;
} // namespace Gui

namespace Gui::State
{
struct FieldPlotSeries {
    static constexpr size_t kCapacity = 100;

    std::array<float, kCapacity> samples{};
    size_t head  = 0;
    size_t count = 0;

    void Clear();
    void Push(float value);
    void CopyOrdered(std::vector<float>& out) const;
    void MinMax(float& outMin, float& outMax) const;
};

struct WatchedField {
    uint32_t    id = 0;
    uintptr_t   baseAddress = 0;
    uint32_t    offset = 0;
    uintptr_t   readAddress = 0;
    std::string className{};
    std::string fieldName{};
    std::string typeName{};
    bool        isStatic = false;
    NavigationSnapshot restoreSnapshot{};
    std::string lastDisplay{};
    bool        stale = false;
    FieldPlotSeries plot{};
    bool        plotEnabled = false;
};

struct FieldWatchModel {
    static constexpr size_t kMaxEntries = 32;

    std::vector<WatchedField> entries{};
    uint32_t                  nextId = 1;
    uint32_t                  selectedPlotWatchId = 0;
    bool                      focusChartsTab      = false;

    bool IsWatchable(const Engine::FieldInfo& field) const;

    bool IsWatching(const ControlPanelSessionState& state, const Engine::FieldInfo& field) const;

    enum class ToggleResult { Added, Removed, RejectedCap, RejectedType };
    ToggleResult Toggle(ControlPanelSessionState& state, const Engine::FieldInfo& field);

    void Remove(uint32_t id);
    void Clear();
    void UpdateLiveValues();

    size_t CountPlottable() const;
    bool   SetSelectedPlotWatchId(uint32_t id);
    bool   ConsumeFocusChartsTab();
    void   EnsureValidPlotSelection();

    const WatchedField* Find(uint32_t id) const;
    WatchedField*       Find(uint32_t id);
};

bool IsWatchableFieldType(const std::string& typeName);
bool IsPlottableFieldType(const std::string& typeName);
bool TrySamplePlottableValue(const std::string& typeName, uintptr_t readAddress, float& out);
} // namespace Gui::State

#endif // ENABLE_DUMPER
