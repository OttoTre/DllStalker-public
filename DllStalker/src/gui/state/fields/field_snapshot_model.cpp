#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/fields/field_snapshot_model.h"

#include "gui/session_state.h"

#include "types/type_classifier.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <string_view>

namespace Gui::State
{
namespace
{
bool IsNumericCategory(Engine::Types::TypeCategory cat) {
    using Cat = Engine::Types::TypeCategory;
    switch (cat) {
    case Cat::I1:
    case Cat::I2:
    case Cat::I4:
    case Cat::I8:
    case Cat::U1:
    case Cat::U2:
    case Cat::U4:
    case Cat::U8:
    case Cat::R4:
    case Cat::R8:
    case Cat::BOOLEAN:
        return true;
    default:
        return false;
    }
}

bool IsUnparseableDisplay(std::string_view text) {
    if (text.empty()) {
        return true;
    }
    if (text.size() >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        return true;
    }
    if (text == "null" || text == "??" || text == "[]") {
        return true;
    }
    return false;
}

bool TryParseDouble(std::string_view text, double& out) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    if (text.empty() || IsUnparseableDisplay(text)) {
        return false;
    }

    const std::string buffer(text);
    char* end = nullptr;
    out = std::strtod(buffer.c_str(), &end);
    if (end == buffer.c_str() || (end && *end != '\0')) {
        return false;
    }
    return std::isfinite(out);
}

void WriteLocalTimeLabel(std::string& out) {
    time_t now = time(nullptr);
    tm localTime{};
    localtime_s(&localTime, &now);
    char buf[32] = {};
    if (strftime(buf, sizeof(buf), "%H:%M:%S", &localTime) > 0) {
        out = buf;
    }
    else {
        out.clear();
    }
}
} // namespace

bool FieldAnalysisScope::operator==(const FieldAnalysisScope& other) const {
    return sidebarClassPtr == other.sidebarClassPtr
        && viewInstancePtr == other.viewInstancePtr
        && breadcrumbCount == other.breadcrumbCount
        && inCollection == other.inCollection
        && collectionFieldName == other.collectionFieldName
        && breadcrumbLabels == other.breadcrumbLabels
        && breadcrumbIsCollection == other.breadcrumbIsCollection;
}

std::string FieldKeyFromInfo(const Engine::FieldInfo& field) {
    return field.name;
}

FieldDiffTint DiffFieldDisplay(const std::string& typeName,
                               const std::string& baselineDisplay,
                               const std::string& liveDisplay) {
    if (baselineDisplay == liveDisplay) {
        return FieldDiffTint::None;
    }

    const auto category = Engine::Types::GetCategory(typeName);

    if (category == Engine::Types::TypeCategory::BOOLEAN) {
        const auto parseBool = [](std::string_view text) -> int {
            if (text == "true") {
                return 1;
            }
            if (text == "false") {
                return 0;
            }
            return -1;
        };
        const int baselineBool = parseBool(baselineDisplay);
        const int liveBool     = parseBool(liveDisplay);
        if (baselineBool < 0 || liveBool < 0) {
            return FieldDiffTint::Changed;
        }
        if (liveBool > baselineBool) {
            return FieldDiffTint::Increased;
        }
        if (liveBool < baselineBool) {
            return FieldDiffTint::Decreased;
        }
        return FieldDiffTint::None;
    }

    if (!IsNumericCategory(category)) {
        return FieldDiffTint::Changed;
    }

    double baselineValue = 0.0;
    double liveValue     = 0.0;
    if (!TryParseDouble(baselineDisplay, baselineValue) || !TryParseDouble(liveDisplay, liveValue)) {
        return FieldDiffTint::Changed;
    }

    if (liveValue > baselineValue) {
        return FieldDiffTint::Increased;
    }
    if (liveValue < baselineValue) {
        return FieldDiffTint::Decreased;
    }
    return FieldDiffTint::Changed;
}

uint32_t FieldDiffTintToColor(FieldDiffTint tint) {
    switch (tint) {
    case FieldDiffTint::Changed:
        return static_cast<uint32_t>(IM_COL32(200, 180, 40, 90));
    case FieldDiffTint::Increased:
        return static_cast<uint32_t>(IM_COL32(50, 170, 70, 90));
    case FieldDiffTint::Decreased:
        return static_cast<uint32_t>(IM_COL32(190, 60, 60, 90));
    default:
        return 0;
    }
}

FieldAnalysisScope BuildFieldAnalysisScope(const ControlPanelSessionState& state) {
    FieldAnalysisScope scope{};
    scope.sidebarClassPtr = state.selectedClass;

    {
        std::lock_guard<std::mutex> lock(state.inspector.mutex);
        scope.viewInstancePtr = state.inspector.cache.activeInstancePtr;
    }

    scope.breadcrumbCount = state.walker.stack.size();
    if (!state.walker.stack.empty()) {
        const InspectorBreadcrumb& top = state.walker.stack.back();
        scope.inCollection         = top.isCollection;
        scope.collectionFieldName  = top.isCollection ? top.sourceField.name : std::string{};
    }

    scope.breadcrumbLabels.reserve(state.walker.stack.size());
    scope.breadcrumbIsCollection.reserve(state.walker.stack.size());
    for (const auto& step : state.walker.stack) {
        scope.breadcrumbLabels.push_back(step.label);
        scope.breadcrumbIsCollection.push_back(step.isCollection);
    }

    return scope;
}

void FieldSnapshotModel::InvalidateIfScopeChanged(const FieldAnalysisScope& scope) {
    if (!HasBaseline()) {
        return;
    }
    if (scope == capturedScope) {
        return;
    }
    ClearBaseline();
}

void FieldSnapshotModel::ClearBaseline() {
    baseline.clear();
    rowTints.clear();
    showChanges        = false;
    lastChangedCount   = 0;
    snapshotFieldCount = 0;
    snapshotTimeLabel.clear();
    capturedScope      = FieldAnalysisScope{};
}

void FieldSnapshotModel::CaptureBaseline(const std::vector<Engine::FieldInfo>& fields,
                                         const FieldAnalysisScope& scope) {
    baseline.clear();
    rowTints.clear();
    lastChangedCount = 0;

    for (const auto& field : fields) {
        FieldSnapshotEntry entry{};
        entry.fieldKey         = FieldKeyFromInfo(field);
        entry.typeName         = field.isEnum && !field.underlyingType.empty()
                             ? field.underlyingType : field.type;
        entry.baselineDisplay  = field.valueDisplay;
        baseline.emplace(entry.fieldKey, std::move(entry));
    }

    capturedScope      = scope;
    snapshotFieldCount = baseline.size();
    WriteLocalTimeLabel(snapshotTimeLabel);
}

void FieldSnapshotModel::RecomputeDiff(const std::vector<Engine::FieldInfo>& liveFields) {
    rowTints.clear();
    lastChangedCount = 0;

    if (!HasBaseline()) {
        return;
    }

    for (const auto& field : liveFields) {
        const std::string key = FieldKeyFromInfo(field);
        const auto        it  = baseline.find(key);
        if (it == baseline.end()) {
            continue;
        }

        const FieldDiffTint tint =
            DiffFieldDisplay(it->second.typeName, it->second.baselineDisplay, field.valueDisplay);
        if (tint != FieldDiffTint::None) {
            rowTints[key] = tint;
            ++lastChangedCount;
        }
    }
}

FieldDiffTint FieldSnapshotModel::TintForField(const std::string& fieldKey) const {
    if (!showChanges) {
        return FieldDiffTint::None;
    }
    const auto it = rowTints.find(fieldKey);
    return it == rowTints.end() ? FieldDiffTint::None : it->second;
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
