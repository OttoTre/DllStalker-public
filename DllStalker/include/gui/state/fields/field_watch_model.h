#pragma once

#include "pch.h"

#ifdef ENABLE_DUMPER

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "gui/state/navigation/inspector_navigation_snapshot.h"
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
    mutable std::mutex        entriesMutex{};
    std::atomic<size_t>       activeCount{0};

    bool IsWatchable(const Engine::FieldInfo& field) const;

    bool IsWatching(const ControlPanelSessionState& state, const Engine::FieldInfo& field) const;

    enum class ToggleResult { Added, Removed, RejectedCap, RejectedType };
    ToggleResult Toggle(ControlPanelSessionState& state, const Engine::FieldInfo& field);

    void Remove(uint32_t id);
    void Clear();
    void SampleOnce();
    std::vector<WatchedField> SnapshotEntries() const;
    size_t HasActiveEntriesCount() const { return activeCount.load(std::memory_order_relaxed); }

    size_t CountPlottable() const;
    bool   SetSelectedPlotWatchId(uint32_t id);
    bool   ConsumeFocusChartsTab();
    void   EnsureValidPlotSelection();

private:
    const WatchedField* FindLocked(uint32_t id) const;
    WatchedField*       FindLocked(uint32_t id);
    void                EnsureValidPlotSelectionLocked();
    void                NotifySampler();
    void                SamplerLoop(std::stop_token stopToken);
    std::condition_variable_any samplerWake{};

public:
    // Must remain last: std::jthread joins before entries/mutex are destroyed.
    std::jthread sampler{ [this](std::stop_token stopToken) { SamplerLoop(stopToken); } };
};

bool IsWatchableFieldType(const std::string& typeName);
bool IsPlottableFieldType(const std::string& typeName);
bool TrySamplePlottableValue(const std::string& typeName, uintptr_t readAddress, float& out);
} // namespace Gui::State

#endif // ENABLE_DUMPER
