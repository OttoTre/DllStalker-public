#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/field_watch_model.h"

#include "gui/session_state.h"
#include "gui/state/field_snapshot_model.h"

#include "types/memory_guard.h"
#include "types/type_classifier.h"
#include "types/value_decoder.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
    if (!state.navigationStack.empty()) {
        instancePtr = state.navigationStack.back().instance;
    }
    else {
        std::lock_guard<std::mutex> lock(state.inspectorCacheMutex);
        instancePtr = state.inspectorCache.activeInstancePtr;
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
    return field.hasValue && field.valueAddress != 0 && IsWatchableFieldType(field.type);
}

bool FieldWatchModel::IsWatching(const ControlPanelSessionState& state,
                                 const Engine::FieldInfo& field) const {
    if (!IsWatchable(field)) {
        return false;
    }

    const NavigationSnapshot currentSnap = state.CaptureNavigationSnapshot("");
    const std::string fieldKey           = FieldKeyFromInfo(field);

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

    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (EntriesMatchField(*it, fieldKey, currentSnap)) {
            entries.erase(it);
            EnsureValidPlotSelection();
            return ToggleResult::Removed;
        }
    }

    if (entries.size() >= kMaxEntries) {
        return ToggleResult::RejectedCap;
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
    entry.typeName        = field.type;
    entry.isStatic        = isStatic;
    entry.restoreSnapshot = currentSnap;
    entry.plotEnabled     = IsPlottableFieldType(field.type);
    entry.plot.Clear();
    const uint32_t newId = entry.id;
    entries.push_back(std::move(entry));
    if (entries.back().plotEnabled) {
        selectedPlotWatchId = newId;
    }
    return ToggleResult::Added;
}

void FieldWatchModel::Remove(uint32_t id) {
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
                       [id](const WatchedField& e) { return e.id == id; }),
        entries.end());
    EnsureValidPlotSelection();
}

void FieldWatchModel::Clear() {
    entries.clear();
    selectedPlotWatchId = 0;
}

size_t FieldWatchModel::CountPlottable() const {
    size_t count = 0;
    for (const auto& entry : entries) {
        if (entry.plotEnabled) {
            ++count;
        }
    }
    return count;
}

bool FieldWatchModel::SetSelectedPlotWatchId(uint32_t id) {
    const WatchedField* entry = Find(id);
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
    if (selectedPlotWatchId != 0) {
        const WatchedField* entry = Find(selectedPlotWatchId);
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

void FieldWatchModel::UpdateLiveValues() {
    for (auto& entry : entries) {
        if (entry.readAddress == 0
            || !Engine::Memory::IsReadablePointer(reinterpret_cast<void*>(entry.readAddress), 1)) {
            entry.stale       = true;
            entry.lastDisplay = "??";
            continue;
        }

        entry.lastDisplay =
            Engine::Decode::DecodeFieldValue(entry.typeName, entry.readAddress, true);
        entry.stale = (entry.lastDisplay == "??" || entry.lastDisplay == "-");

        if (!entry.plotEnabled || entry.stale) {
            continue;
        }

        float sample = 0.0f;
        if (TrySamplePlottableValue(entry.typeName, entry.readAddress, sample)) {
            entry.plot.Push(sample);
        }
    }
}

const WatchedField* FieldWatchModel::Find(uint32_t id) const {
    for (const auto& entry : entries) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

WatchedField* FieldWatchModel::Find(uint32_t id) {
    for (auto& entry : entries) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
