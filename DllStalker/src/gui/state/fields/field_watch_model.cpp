#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/fields/field_watch_model.h"

#include "gui/session_state.h"
#include "gui/state/fields/field_snapshot_model.h"

#include "types/memory_guard.h"
#include "types/type_classifier.h"
#include "types/value_decoder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <thread>
#include <utility>

namespace Gui::State
{
void FieldPlotSeries::Clear() {
    head  = 0;
    count = 0;
}

void FieldPlotSeries::Push(float value) {
    samples[head] = value;
    head          = (head + 1) % kCapacity;
    if (count < kCapacity) {
        ++count;
    }
}

void FieldPlotSeries::CopyOrdered(std::vector<float>& out) const {
    out.clear();
    if (count == 0) {
        return;
    }
    out.resize(count);
    if (count < kCapacity) {
        for (size_t i = 0; i < count; ++i) {
            out[i] = samples[i];
        }
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        out[i] = samples[(head + i) % kCapacity];
    }
}

void FieldPlotSeries::MinMax(float& outMin, float& outMax) const {
    outMin = (std::numeric_limits<float>::max)();
    outMax = std::numeric_limits<float>::lowest();
    if (count == 0) {
        outMin = 0.0f;
        outMax = 0.0f;
        return;
    }
    std::vector<float> ordered;
    CopyOrdered(ordered);
    for (float v : ordered) {
        if (v < outMin) {
            outMin = v;
        }
        if (v > outMax) {
            outMax = v;
        }
    }
}

namespace
{
bool IsFiniteFloat(float v) {
    return std::isfinite(static_cast<double>(v));
}
bool TryBuildWatchAddresses(const ControlPanelSessionState& state,
                            const Engine::FieldInfo& field,
                            uintptr_t& baseAddress,
                            uint32_t& offset,
                            uintptr_t& readAddress,
                            bool& isStatic) {
    if (!field.hasValue || field.valueAddress == 0) {
        return false;
    }

    readAddress = field.valueAddress;
    isStatic    = (field.staticValue != 0 && field.valueAddress == field.staticValue);
    if (isStatic) {
        baseAddress = field.valueAddress;
        offset      = 0;
        return true;
    }

    void* instancePtr = nullptr;
    if (!state.walker.stack.empty()) {
        instancePtr = state.walker.stack.back().instance;
    }
    else {
        std::lock_guard<std::mutex> lock(state.inspector.mutex);
        instancePtr = state.inspector.cache.activeInstancePtr;
    }

    if (instancePtr != nullptr
        && field.valueAddress == reinterpret_cast<uintptr_t>(instancePtr) + field.offset) {
        baseAddress = reinterpret_cast<uintptr_t>(instancePtr);
        offset      = static_cast<uint32_t>(field.offset);
        return true;
    }

    if (field.offset > 0 && field.valueAddress >= field.offset) {
        baseAddress = field.valueAddress - field.offset;
    }
    else {
        baseAddress = field.valueAddress;
    }
    offset = static_cast<uint32_t>(field.offset);
    return true;
}

bool EntriesMatchField(const WatchedField& entry,
                       const std::string& fieldKey,
                       const NavigationSnapshot& currentSnap) {
    return entry.fieldName == fieldKey
        && NavigationFingerprintsEqual(entry.restoreSnapshot, currentSnap);
}
} // namespace

bool IsWatchableFieldType(const std::string& typeName) {
    using Cat = Engine::Types::TypeCategory;
    const Cat cat = Engine::Types::GetCategory(typeName);
    switch (cat) {
    case Cat::UNKNOWN:
    case Cat::PTR:
    case Cat::ARRAY:
    case Cat::LIST:
        return false;
    default:
        return true;
    }
}

bool IsPlottableFieldType(const std::string& typeName) {
    using Cat = Engine::Types::TypeCategory;
    switch (Engine::Types::GetCategory(typeName)) {
    case Cat::I4:
    case Cat::I8:
    case Cat::R4:
    case Cat::R8:
        return true;
    default:
        return false;
    }
}

bool TrySamplePlottableValue(const std::string& typeName, uintptr_t readAddress, float& out) {
    if (!readAddress) {
        return false;
    }

    using Cat = Engine::Types::TypeCategory;
    switch (Engine::Types::GetCategory(typeName)) {
    case Cat::I4: {
        int32_t v = 0;
        if (!Engine::Memory::TryReadValue(readAddress, v)) {
            return false;
        }
        out = static_cast<float>(v);
        return IsFiniteFloat(out);
    }
    case Cat::I8: {
        int64_t v = 0;
        if (!Engine::Memory::TryReadValue(readAddress, v)) {
            return false;
        }
        out = static_cast<float>(v);
        return IsFiniteFloat(out);
    }
    case Cat::R4: {
        float v = 0.0f;
        if (!Engine::Memory::TryReadValue(readAddress, v)) {
            return false;
        }
        if (!IsFiniteFloat(v)) {
            return false;
        }
        out = v;
        return true;
    }
    case Cat::R8: {
        double v = 0.0;
        if (!Engine::Memory::TryReadValue(readAddress, v)) {
            return false;
        }
        if (!std::isfinite(v)) {
            return false;
        }
        out = static_cast<float>(v);
        return IsFiniteFloat(out);
    }
    default:
        return false;
    }
}

bool FieldWatchModel::IsWatchable(const Engine::FieldInfo& field) const {
    if (!field.hasValue || field.valueAddress == 0) {
        return false;
    }
    if (field.isEnum && !field.underlyingType.empty()) {
        return IsWatchableFieldType(field.underlyingType);
    }
    return IsWatchableFieldType(field.type);
}

bool FieldWatchModel::IsWatching(const ControlPanelSessionState& state,
                                 const Engine::FieldInfo& field) const {
    if (!IsWatchable(field)) {
        return false;
    }

    const NavigationSnapshot currentSnap = state.CaptureNavigationSnapshot("");
    const std::string fieldKey           = FieldKeyFromInfo(field);

    std::lock_guard<std::mutex> lock(entriesMutex);
    for (const auto& entry : entries) {
        if (EntriesMatchField(entry, fieldKey, currentSnap)) {
            return true;
        }
    }
    return false;
}

FieldWatchModel::ToggleResult FieldWatchModel::Toggle(ControlPanelSessionState& state,
                                                      const Engine::FieldInfo& field) {
    if (!IsWatchable(field)) {
        return ToggleResult::RejectedType;
    }

    const NavigationSnapshot currentSnap = state.CaptureNavigationSnapshot("");
    const std::string fieldKey           = FieldKeyFromInfo(field);

    {
        std::lock_guard<std::mutex> lock(entriesMutex);
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            if (EntriesMatchField(*it, fieldKey, currentSnap)) {
                entries.erase(it);
                activeCount.store(entries.size(), std::memory_order_relaxed);
                EnsureValidPlotSelectionLocked();
                NotifySampler();
                return ToggleResult::Removed;
            }
        }

        if (entries.size() >= kMaxEntries) {
            return ToggleResult::RejectedCap;
        }
    }

    uintptr_t baseAddress = 0;
    uint32_t  offset      = 0;
    uintptr_t readAddress = 0;
    bool      isStatic    = false;
    if (!TryBuildWatchAddresses(state, field, baseAddress, offset, readAddress, isStatic)) {
        return ToggleResult::RejectedType;
    }

    WatchedField entry{};
    entry.id              = nextId++;
    entry.baseAddress     = baseAddress;
    entry.offset          = offset;
    entry.readAddress     = readAddress;
    entry.className       = currentSnap.className;
    entry.fieldName       = fieldKey;
    entry.typeName        = field.isEnum && !field.underlyingType.empty()
                          ? field.underlyingType : field.type;
    entry.isStatic        = isStatic;
    entry.restoreSnapshot = currentSnap;
    entry.plotEnabled     = IsPlottableFieldType(entry.typeName);
    entry.plot.Clear();
    const uint32_t newId = entry.id;

    std::lock_guard<std::mutex> lock(entriesMutex);
    if (entries.size() >= kMaxEntries) {
        return ToggleResult::RejectedCap;
    }
    entries.push_back(std::move(entry));
    activeCount.store(entries.size(), std::memory_order_relaxed);
    if (entries.back().plotEnabled) {
        selectedPlotWatchId = newId;
    }
    NotifySampler();
    return ToggleResult::Added;
}

void FieldWatchModel::Remove(uint32_t id) {
    std::lock_guard<std::mutex> lock(entriesMutex);
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
                       [id](const WatchedField& e) { return e.id == id; }),
        entries.end());
    activeCount.store(entries.size(), std::memory_order_relaxed);
    EnsureValidPlotSelectionLocked();
    NotifySampler();
}

void FieldWatchModel::Clear() {
    std::lock_guard<std::mutex> lock(entriesMutex);
    entries.clear();
    activeCount.store(0, std::memory_order_relaxed);
    selectedPlotWatchId = 0;
    NotifySampler();
}

size_t FieldWatchModel::CountPlottable() const {
    std::lock_guard<std::mutex> lock(entriesMutex);
    size_t count = 0;
    for (const auto& entry : entries) {
        if (entry.plotEnabled) {
            ++count;
        }
    }
    return count;
}

bool FieldWatchModel::SetSelectedPlotWatchId(uint32_t id) {
    std::lock_guard<std::mutex> lock(entriesMutex);
    const WatchedField* entry = FindLocked(id);
    if (!entry || !entry->plotEnabled) {
        return false;
    }
    selectedPlotWatchId = id;
    return true;
}

bool FieldWatchModel::ConsumeFocusChartsTab() {
    if (!focusChartsTab) {
        return false;
    }
    focusChartsTab = false;
    return true;
}

void FieldWatchModel::EnsureValidPlotSelection() {
    std::lock_guard<std::mutex> lock(entriesMutex);
    EnsureValidPlotSelectionLocked();
}

void FieldWatchModel::EnsureValidPlotSelectionLocked() {
    if (selectedPlotWatchId != 0) {
        const WatchedField* entry = FindLocked(selectedPlotWatchId);
        if (entry != nullptr && entry->plotEnabled) {
            return;
        }
    }

    selectedPlotWatchId = 0;
    for (const auto& entry : entries) {
        if (entry.plotEnabled) {
            selectedPlotWatchId = entry.id;
            return;
        }
    }
}

void FieldWatchModel::SampleOnce() {
    struct SampleJob {
        uint32_t id = 0;
        uintptr_t readAddress = 0;
        std::string typeName{};
        bool plotEnabled = false;
    };
    struct SampleResult {
        uint32_t id = 0;
        std::string display{};
        bool stale = false;
        bool hasSample = false;
        float sample = 0.0f;
    };

    std::vector<SampleJob> jobs;
    {
        std::lock_guard<std::mutex> lock(entriesMutex);
        jobs.reserve(entries.size());
        for (const auto& entry : entries) {
            jobs.push_back({ entry.id, entry.readAddress, entry.typeName, entry.plotEnabled });
        }
    }

    std::vector<SampleResult> results;
    results.reserve(jobs.size());
    for (const auto& job : jobs) {
        SampleResult result{};
        result.id = job.id;

        if (job.readAddress == 0
            || !Engine::Memory::IsReadablePointer(reinterpret_cast<void*>(job.readAddress), 1)) {
            result.stale = true;
            result.display = "??";
            results.push_back(std::move(result));
            continue;
        }

        result.display = Engine::Decode::DecodeFieldValue(job.typeName, job.readAddress, true);
        result.stale = (result.display == "??" || result.display == "-");

        if (!job.plotEnabled || result.stale) {
            results.push_back(std::move(result));
            continue;
        }

        float sample = 0.0f;
        if (TrySamplePlottableValue(job.typeName, job.readAddress, sample)) {
            result.hasSample = true;
            result.sample = sample;
        }
        results.push_back(std::move(result));
    }

    std::lock_guard<std::mutex> lock(entriesMutex);
    for (auto& result : results) {
        WatchedField* entry = FindLocked(result.id);
        if (!entry) {
            continue;
        }
        entry->lastDisplay = std::move(result.display);
        entry->stale = result.stale;
        if (result.hasSample) {
            entry->plot.Push(result.sample);
        }
    }
}

std::vector<WatchedField> FieldWatchModel::SnapshotEntries() const {
    std::lock_guard<std::mutex> lock(entriesMutex);
    return entries;
}

void FieldWatchModel::NotifySampler() {
    samplerWake.notify_all();
}

void FieldWatchModel::SamplerLoop(std::stop_token stopToken) {
    using namespace std::chrono_literals;
    while (!stopToken.stop_requested()) {
        {
            std::unique_lock<std::mutex> lock(entriesMutex);
            samplerWake.wait(lock, stopToken, [&] { return !entries.empty(); });
        }

        if (stopToken.stop_requested()) {
            break;
        }

        SampleOnce();

        {
            std::unique_lock<std::mutex> lock(entriesMutex);
            samplerWake.wait_for(lock, stopToken, 100ms, [&] { return entries.empty(); });
        }
    }
}

const WatchedField* FieldWatchModel::FindLocked(uint32_t id) const {
    for (const auto& entry : entries) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

WatchedField* FieldWatchModel::FindLocked(uint32_t id) {
    for (auto& entry : entries) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
