#pragma once

#include "build_config.h"

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
enum class WatchPlotMode { EverySample, OnChange };

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
    WatchPlotMode plotMode = WatchPlotMode::EverySample;
    bool        hasCustomPlotMode = false;
    bool        hasLastPlotSample = false;
    float       lastPlotSample = 0.0f;
    std::string lastPlotDisplay{};
};

struct FieldWatchModel {
    static constexpr size_t kMaxEntries = 32;
    static constexpr size_t kSampleIntervalCount = 6;
    static constexpr std::array<int, kSampleIntervalCount> kSampleIntervalMs = {
        100, 250, 500, 1000, 2000, 5000
    };

    std::vector<WatchedField> entries{};
    uint32_t                  nextId = 1;
    uint32_t                  selectedPlotWatchId = 0;
    bool                      focusChartsTab      = false;
    int                       sampleIntervalIndex = 0;
    int                       defaultPlotModeIndex = 0;
    mutable std::mutex        entriesMutex{};
    std::atomic<size_t>       activeCount{0};
    std::atomic<int>          sampleIntervalMs{100};
    std::atomic<uint64_t>     samplerWakeGeneration{0};

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
    bool   SetSampleIntervalIndex(int index);
    bool   SetDefaultPlotModeIndex(int index);
    bool   SetEntryPlotMode(uint32_t id, WatchPlotMode mode, bool custom);
    WatchPlotMode DefaultPlotMode() const;
    bool   ConsumeFocusChartsTab();
    void   EnsureValidPlotSelection();

private:
    const WatchedField* FindLocked(uint32_t id) const;
    WatchedField*       FindLocked(uint32_t id);
    void                EnsureValidPlotSelectionLocked();
    void                ResetPlotTrackingLocked(WatchedField& entry, bool clearPlot);
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
