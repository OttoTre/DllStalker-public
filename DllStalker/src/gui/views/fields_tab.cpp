#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/fields_tab.h"

#include "gui/config.h"
#include "gui/infra/search_filter.h"
#include "gui/state/field_path_resolver.h"
#include "gui/state/field_snapshot_model.h"
#include "gui/state/field_watch_model.h"
#include "gui/state/history_steady_time.h"
#include "types/type_classifier.h"

#include "imgui.h"

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace Gui::Views
{
namespace
{
constexpr float FIELD_REFRESH_INTERVALS[] = { 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };
constexpr const char* FIELD_REFRESH_INTERVAL_LABELS[] = { "0.5s", "1s", "2s", "3s", "4s", "5s" };
constexpr const char* INSTANCE_SEARCH_MODE_LABELS[] = { "Static discovery", "Live API" };

// Single source of truth: any numeric / boolean category is editable.
// Excluded categories:
//   STRING        — managed string editing isn't implemented yet
//   PTR           — overwriting a managed reference is unsafe and would
//                   bypass GC bookkeeping
//   ARRAY / LIST  — these are navigation targets (open in the Walker),
//                   not values to overwrite. Falling into the edit
//                   branch would also hide the green drill-in link.
//   UNKNOWN       — we don't know how to parse it
bool IsEditableFieldType(const std::string& typeName) {
    using Cat = Engine::Types::TypeCategory;
    const Cat cat = Engine::Types::GetCategory(typeName);
    return cat != Cat::UNKNOWN
        && cat != Cat::STRING
        && cat != Cat::PTR
        && cat != Cat::ARRAY
        && cat != Cat::LIST;
}

std::string LookupSidebarClassName(const ControlPanelSessionState& state) {
    if (!state.selectedClass) {
        return {};
    }
    for (const auto& cl : state.GetClassCacheSnapshot()) {
        if (cl.klassPtr == state.selectedClass) {
            if (!cl.ns.empty()) {
                return cl.ns + "::" + cl.name;
            }
            return cl.name;
        }
    }
    return "<class>";
}

struct TwoInstanceCompareRow {
    std::string name{};
    std::string type{};
    std::string valueA{};
    std::string valueB{};
    bool        differs = false;
};

enum class TwoInstanceCompareMode {
    ClassFields  = 0,
    CurrentPath  = 1,
};

struct TwoInstanceModalState {
    bool     openRequested        = false;
    int      instanceIndexA       = 0;
    int      instanceIndexB       = 1;
    int      compareMode          = static_cast<int>(TwoInstanceCompareMode::ClassFields);
    int      collectionElementIndex = 0;
    int      selectedLeafFieldIndex   = 0;
    bool     hasCompareResult     = false;
    std::vector<TwoInstanceCompareRow> rows{};
    bool     hasPathResult        = false;
    std::string pathLabel{};
    std::string pathValueA{};
    std::string pathValueB{};
    std::string pathErrorA{};
    std::string pathErrorB{};
    bool     pathDiffers          = false;
};

TwoInstanceModalState& TwoInstanceModal() {
    static TwoInstanceModalState s_state;
    return s_state;
}

void RenderTwoInstanceCompareModal(ControlPanelSessionState& state,
                                 const InspectorCache& inspectorSnapshot) {
    auto& modal = TwoInstanceModal();

    if (!ImGui::BeginPopupModal("CompareTwoInstancesPopup", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const std::vector<void*>& instances = state.rootInstanceCandidates;
    const bool inCollectionView =
        !state.navigationStack.empty() && state.navigationStack.back().isCollection;
    const bool pathContextAvailable = state.navigationStack.size() > 1;

    if (instances.size() < 2 || !state.selectedClass || !state.dumper) {
        ImGui::TextUnformatted(
            "Need at least two root instance candidates (use Find Instances at class root).");
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            modal.hasCompareResult = false;
            modal.hasPathResult    = false;
            modal.rows.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    if (inCollectionView
        && modal.compareMode == static_cast<int>(TwoInstanceCompareMode::ClassFields)) {
        modal.compareMode = static_cast<int>(TwoInstanceCompareMode::CurrentPath);
    }

    auto instanceLabel = [&instances](int index) -> std::string {
        char buffer[64] = {};
        snprintf(buffer, sizeof(buffer), "[%d] %p", index, instances[static_cast<size_t>(index)]);
        return buffer;
    };

    if (modal.instanceIndexA < 0 || modal.instanceIndexA >= static_cast<int>(instances.size())) {
        modal.instanceIndexA = 0;
    }
    if (modal.instanceIndexB < 0 || modal.instanceIndexB >= static_cast<int>(instances.size())) {
        modal.instanceIndexB = instances.size() > 1 ? 1 : 0;
    }
    if (modal.instanceIndexA == modal.instanceIndexB && instances.size() > 1) {
        modal.instanceIndexB = (modal.instanceIndexA + 1) % static_cast<int>(instances.size());
    }

    const int classMode = static_cast<int>(TwoInstanceCompareMode::ClassFields);
    const int pathMode  = static_cast<int>(TwoInstanceCompareMode::CurrentPath);

    if (inCollectionView) {
        ImGui::BeginDisabled();
    }
    if (ImGui::RadioButton("Class fields", &modal.compareMode, classMode)) {
    }
    if (inCollectionView) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                "Class-field compare is only available outside a collection view. Use Current path.");
        }
    }

    ImGui::SameLine();
    if (!pathContextAvailable) {
        ImGui::BeginDisabled();
    }
    if (ImGui::RadioButton("Current path", &modal.compareMode, pathMode)) {
    }
    if (!pathContextAvailable) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Drill into a member field first.");
        }
    }

    if (modal.compareMode == pathMode && pathContextAvailable) {
        modal.pathLabel = State::BuildFieldPathLabel(state.navigationStack,
                                                     inCollectionView ? modal.collectionElementIndex
                                                                      : -1);
        ImGui::TextDisabled("Path: %s", modal.pathLabel.c_str());

        if (inCollectionView) {
            const int elementCount = static_cast<int>(inspectorSnapshot.fields.size());
            if (elementCount <= 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                                   "Collection has no elements to compare.");
            }
            else {
                if (modal.collectionElementIndex < 0) {
                    modal.collectionElementIndex = 0;
                }
                if (modal.collectionElementIndex >= elementCount) {
                    modal.collectionElementIndex = elementCount - 1;
                }
                ImGui::SetNextItemWidth(120.0f);
                ImGui::SliderInt("Element index", &modal.collectionElementIndex, 0,
                                 elementCount - 1);
            }
        }
        else if (!inspectorSnapshot.fields.empty()) {
            if (modal.selectedLeafFieldIndex < 0
                || modal.selectedLeafFieldIndex
                       >= static_cast<int>(inspectorSnapshot.fields.size())) {
                modal.selectedLeafFieldIndex = 0;
            }
            const auto& leafField =
                inspectorSnapshot.fields[static_cast<size_t>(modal.selectedLeafFieldIndex)];
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::BeginCombo("Member field", leafField.name.c_str())) {
                for (int i = 0; i < static_cast<int>(inspectorSnapshot.fields.size()); ++i) {
                    const bool selected = (i == modal.selectedLeafFieldIndex);
                    if (ImGui::Selectable(inspectorSnapshot.fields[static_cast<size_t>(i)].name.c_str(),
                                          selected)) {
                        modal.selectedLeafFieldIndex = i;
                    }
                }
                ImGui::EndCombo();
            }
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                               "No fields loaded for the current object.");
        }
        ImGui::Spacing();
    }
    else if (modal.compareMode == classMode && state.navigationStack.size() > 1) {
        const std::string classLabel = LookupSidebarClassName(state);
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.35f, 1.0f),
                           "Class fields mode compares declared fields of %s.",
                           classLabel.empty() ? "<class>" : classLabel.c_str());
        ImGui::Spacing();
    }

    ImGui::SetNextItemWidth(280.0f);
    if (ImGui::BeginCombo("Instance A", instanceLabel(modal.instanceIndexA).c_str())) {
        for (int i = 0; i < static_cast<int>(instances.size()); ++i) {
            if (ImGui::Selectable(instanceLabel(i).c_str(), i == modal.instanceIndexA)) {
                modal.instanceIndexA = i;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SetNextItemWidth(280.0f);
    if (ImGui::BeginCombo("Instance B", instanceLabel(modal.instanceIndexB).c_str())) {
        for (int i = 0; i < static_cast<int>(instances.size()); ++i) {
            if (ImGui::Selectable(instanceLabel(i).c_str(), i == modal.instanceIndexB)) {
                modal.instanceIndexB = i;
            }
        }
        ImGui::EndCombo();
    }

    const bool runEnabled = modal.instanceIndexA != modal.instanceIndexB
        && (modal.compareMode != pathMode
            || (pathContextAvailable
                && (!inCollectionView || !inspectorSnapshot.fields.empty())));

    if (!runEnabled) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Run compare", ImVec2(120, 0))) {
        modal.hasCompareResult = false;
        modal.hasPathResult    = false;
        modal.rows.clear();

        if (modal.instanceIndexA == modal.instanceIndexB) {
            // disabled
        }
        else if (modal.compareMode == classMode) {
            void* const instanceA = instances[static_cast<size_t>(modal.instanceIndexA)];
            void* const instanceB = instances[static_cast<size_t>(modal.instanceIndexB)];

            const auto fieldsA =
                state.dumper->GetRawFields(state.selectedClass, instanceA);
            const auto fieldsB =
                state.dumper->GetRawFields(state.selectedClass, instanceB);

            std::unordered_map<std::string, Engine::FieldInfo> byNameB;
            byNameB.reserve(fieldsB.size());
            for (const auto& field : fieldsB) {
                byNameB[field.name] = field;
            }

            modal.rows.reserve(fieldsA.size() + byNameB.size());
            std::unordered_map<std::string, bool> seen;

            for (const auto& fieldA : fieldsA) {
                seen[fieldA.name] = true;
                TwoInstanceCompareRow row{};
                row.name = fieldA.name;
                row.type = fieldA.type;
                row.valueA =
                    fieldA.valueDisplay.empty() ? std::string("-") : fieldA.valueDisplay;

                const auto itB = byNameB.find(fieldA.name);
                if (itB == byNameB.end()) {
                    row.valueB  = "-";
                    row.differs = true;
                }
                else {
                    row.valueB = itB->second.valueDisplay.empty()
                                     ? std::string("-")
                                     : itB->second.valueDisplay;
                    row.differs = row.valueA != row.valueB;
                }
                modal.rows.push_back(std::move(row));
            }

            for (const auto& fieldB : fieldsB) {
                if (seen[fieldB.name]) {
                    continue;
                }
                TwoInstanceCompareRow row{};
                row.name    = fieldB.name;
                row.type    = fieldB.type;
                row.valueA  = "-";
                row.valueB  = fieldB.valueDisplay.empty() ? std::string("-") : fieldB.valueDisplay;
                row.differs = true;
                modal.rows.push_back(std::move(row));
            }

            modal.hasCompareResult = true;
        }
        else {
            const auto steps = State::BuildFieldPathSteps(state.navigationStack);
            std::string leafFieldName{};
            if (!inCollectionView && !inspectorSnapshot.fields.empty()
                && modal.selectedLeafFieldIndex >= 0
                && modal.selectedLeafFieldIndex
                       < static_cast<int>(inspectorSnapshot.fields.size())) {
                leafFieldName =
                    inspectorSnapshot.fields[static_cast<size_t>(modal.selectedLeafFieldIndex)]
                        .name;
            }

            const int collectionIndex =
                inCollectionView ? modal.collectionElementIndex : -1;
            modal.pathLabel =
                State::BuildFieldPathLabel(state.navigationStack, collectionIndex);

            void* const instanceA = instances[static_cast<size_t>(modal.instanceIndexA)];
            void* const instanceB = instances[static_cast<size_t>(modal.instanceIndexB)];

            const State::FieldPathCompareResult resultA = State::ResolvePathValue(
                *state.dumper, state.selectedClass, instanceA, steps, collectionIndex,
                leafFieldName);
            const State::FieldPathCompareResult resultB = State::ResolvePathValue(
                *state.dumper, state.selectedClass, instanceB, steps, collectionIndex,
                leafFieldName);

            modal.pathErrorA.clear();
            modal.pathErrorB.clear();
            if (resultA.ok) {
                modal.pathValueA = resultA.valueDisplay.empty() ? std::string("-")
                                                                 : resultA.valueDisplay;
            }
            else {
                modal.pathValueA = "-";
                modal.pathErrorA = resultA.error;
            }
            if (resultB.ok) {
                modal.pathValueB = resultB.valueDisplay.empty() ? std::string("-")
                                                                 : resultB.valueDisplay;
            }
            else {
                modal.pathValueB = "-";
                modal.pathErrorB = resultB.error;
            }

            modal.pathDiffers =
                resultA.ok && resultB.ok && modal.pathValueA != modal.pathValueB;
            modal.hasPathResult = true;
        }
    }
    if (!runEnabled) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(120, 0))) {
        modal.hasCompareResult = false;
        modal.hasPathResult    = false;
        modal.rows.clear();
        ImGui::CloseCurrentPopup();
    }

    if (modal.compareMode == classMode && modal.hasCompareResult && !modal.rows.empty()) {
        ImGui::Separator();
        if (ImGui::BeginTable("TwoInstanceCompareTable", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                  | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                              ImVec2(520.0f, 260.0f))) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Value A");
            ImGui::TableSetupColumn("Value B");
            ImGui::TableSetupColumn("Type");
            ImGui::TableHeadersRow();

            for (const auto& row : modal.rows) {
                ImGui::TableNextRow();
                if (row.differs) {
                    ImGui::TableSetBgColor(
                        ImGuiTableBgTarget_RowBg0,
                        static_cast<ImU32>(
                            State::FieldDiffTintToColor(State::FieldDiffTint::Changed)));
                }
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(row.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(row.valueA.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(row.valueB.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(row.type.c_str());
            }
            ImGui::EndTable();
        }
    }
    else if (modal.compareMode == pathMode && modal.hasPathResult) {
        ImGui::Separator();
        if (ImGui::BeginTable("TwoInstancePathCompare", 2,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg,
                              ImVec2(520.0f, 0.0f))) {
            ImGui::TableSetupColumn("Instance");
            ImGui::TableSetupColumn("Value");
            ImGui::TableHeadersRow();

            const auto drawPathRow = [&](const char* label, const std::string& value,
                                         const std::string& error, bool highlight) {
                ImGui::TableNextRow();
                if (highlight) {
                    ImGui::TableSetBgColor(
                        ImGuiTableBgTarget_RowBg0,
                        static_cast<ImU32>(
                            State::FieldDiffTintToColor(State::FieldDiffTint::Changed)));
                }
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(label);
                ImGui::TableSetColumnIndex(1);
                if (!error.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s", error.c_str());
                }
                else {
                    ImGui::TextUnformatted(value.c_str());
                }
            };

            drawPathRow("A", modal.pathValueA, modal.pathErrorA, modal.pathDiffers);
            drawPathRow("B", modal.pathValueB, modal.pathErrorB, modal.pathDiffers);
            ImGui::EndTable();
        }
    }

    ImGui::EndPopup();
}

void RenderFieldAnalysisToolbar(ControlPanelSessionState& state,
                                const InspectorCache& inspectorSnapshot,
                                bool inCollectionView,
                                bool fieldsBusy) {
    const State::FieldAnalysisScope scope = State::BuildFieldAnalysisScope(state);
    state.fieldSnapshot.InvalidateIfScopeChanged(scope);

    const bool canSnapshot =
        !fieldsBusy && !inspectorSnapshot.fields.empty() && state.selectedClass != nullptr;

    if (!canSnapshot) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Snapshot state", ImVec2(130, 0))) {
        state.fieldSnapshot.CaptureBaseline(inspectorSnapshot.fields, scope);
    }
    if (!canSnapshot) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Wait for fields to finish loading, or load fields first.");
        }
    }

    ImGui::SameLine();
    if (!state.fieldSnapshot.HasBaseline()) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Clear snapshot", ImVec2(120, 0))) {
        state.fieldSnapshot.ClearBaseline();
    }
    if (!state.fieldSnapshot.HasBaseline()) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (!state.fieldSnapshot.HasBaseline()) {
        ImGui::BeginDisabled();
    }
    ImGui::Checkbox("Show changes", &state.fieldSnapshot.showChanges);
    if (!state.fieldSnapshot.HasBaseline()) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Take a snapshot first.");
        }
    }

    ImGui::SameLine();
    const size_t rootCandidateCount = state.rootInstanceCandidates.size();
    const bool canCompareTwo =
        rootCandidateCount >= 2 && state.selectedClass != nullptr;
    if (!canCompareTwo) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Compare two instances...", ImVec2(200, 0))) {
        TwoInstanceModal().openRequested = true;
    }
    if (!canCompareTwo) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                "At class root, use Find Instances to discover at least two candidates.");
        }
    }
    else if (ImGui::IsItemHovered()) {
        if (inCollectionView) {
            ImGui::SetTooltip(
                "Opens compare modal. Use Current path with an element index for A vs B here; "
                "class-field table is disabled in collection view.");
        }
        else if (state.navigationStack.size() > 1) {
            ImGui::SetTooltip(
                "Class fields = sidebar class layout. Current path = value at your drill path.");
        }
    }

    ImGui::SameLine();
    if (state.fieldSnapshot.HasBaseline()) {
        if (!state.fieldSnapshot.snapshotTimeLabel.empty()) {
            ImGui::TextDisabled("Snapshot: %zu fields @ %s",
                                state.fieldSnapshot.snapshotFieldCount,
                                state.fieldSnapshot.snapshotTimeLabel.c_str());
        }
        else {
            ImGui::TextDisabled("Snapshot: %zu fields",
                                state.fieldSnapshot.snapshotFieldCount);
        }
        if (state.fieldSnapshot.showChanges) {
            ImGui::SameLine();
            ImGui::TextDisabled("| %zu changed", state.fieldSnapshot.lastChangedCount);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Counts all changed fields; filter may hide some rows.");
            }
        }
    }
    else {
        ImGui::TextDisabled("No snapshot");
    }

    if (TwoInstanceModal().openRequested) {
        ImGui::OpenPopup("CompareTwoInstancesPopup");
        TwoInstanceModal().openRequested = false;
    }
}
} // namespace

void RenderFieldsTab(const InspectorCache& inspectorSnapshot,
                     AppShell::CopyFeedbackState& copyFeedback,
                     bool inspectorLoadInProgress,
                     ControlPanelSessionState& state) {
    static char editStatus[128] = {};
    static float editStatusAtSeconds = -1000.0f;
    static char lastRefreshTimeStr[32] = {};
    static char lastRefreshLabel[16] = {};

    const bool inCollectionViewEarly = !state.navigationStack.empty()
                                    && state.navigationStack.back().isCollection;
    const bool fieldsBusy = state.fieldsLoadInProgress.load()
                         || state.inspectorLoadInProgress.load();

    auto WriteTimestamp = [](char* buf, size_t bufSize) {
        time_t now = time(nullptr);
        tm localTime{};
        localtime_s(&localTime, &now);
        strftime(buf, bufSize, "%H:%M:%S", &localTime);
    };

    // When the user is drilled into a collection breadcrumb, "refresh" means
    // re-synthesizing the element rows (the underlying array can be
    // GC-relocated and its length can change between ticks). In every other
    // breadcrumb we keep the existing field-load path.
    const bool inCollectionView = inCollectionViewEarly;
    const Engine::FieldInfo collectionSource = inCollectionView
        ? state.navigationStack.back().sourceField
        : Engine::FieldInfo{};

    auto DoRefresh = [&](bool isManual) {
        if (state.fieldsLoadInProgress.load() || state.inspectorLoadInProgress.load()
            || !state.dumper || !state.selectedClass) {
            return;
        }
        state.editBuffers.clear();
        state.fieldsLastRefreshAt = ImGui::GetTime();
        if (inCollectionView) {
            state.StartCollectionLoad(state.dumper, collectionSource);
        }
        else {
            state.StartFieldsLoad(state.dumper, state.selectedClass);
        }
        WriteTimestamp(lastRefreshTimeStr, sizeof(lastRefreshTimeStr));
        strncpy_s(lastRefreshLabel, sizeof(lastRefreshLabel), isManual ? "Manual" : "Auto", _TRUNCATE);
    };

    if (inspectorSnapshot.fields.empty() && !inspectorLoadInProgress && !state.fieldsLoadInProgress.load()) {
        ImGui::TextUnformatted("No fields available.");
    }

    if (ImGui::Button("Refresh Fields", ImVec2(140, 0))) {
        DoRefresh(true);
    }

    ImGui::SameLine();
    ImGui::Checkbox("Auto refresh", &state.fieldsAutoRefresh);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::Combo("##FieldsRefreshInterval", &state.fieldsRefreshIntervalIndex, FIELD_REFRESH_INTERVAL_LABELS, IM_ARRAYSIZE(FIELD_REFRESH_INTERVAL_LABELS));

    if (state.fieldsAutoRefresh && state.dumper && state.selectedClass
        && !state.fieldsLoadInProgress.load()
        && !(inCollectionView && state.inspectorLoadInProgress.load())) {
        const int intervalIndex = (state.fieldsRefreshIntervalIndex >= 0 && state.fieldsRefreshIntervalIndex < IM_ARRAYSIZE(FIELD_REFRESH_INTERVALS))
            ? state.fieldsRefreshIntervalIndex
            : 1;

        const double now = ImGui::GetTime();
        const double intervalSeconds = static_cast<double>(FIELD_REFRESH_INTERVALS[intervalIndex]);
        if (state.fieldsLastRefreshAt <= 0.0 || (now - state.fieldsLastRefreshAt) >= intervalSeconds) {
            DoRefresh(false);
        }
    }

    RenderFieldAnalysisToolbar(state, inspectorSnapshot, inCollectionView, fieldsBusy);

    if (state.fieldSnapshot.showChanges && state.fieldSnapshot.HasBaseline() && !fieldsBusy) {
        state.fieldSnapshot.RecomputeDiff(inspectorSnapshot.fields);
    }

    // The candidate-source picker and the candidates combobox only make sense
    // at the navigation root. While the user is drilled into a nested object
    // (breadcrumb depth > 1) the active instance is determined by the walker,
    // not by candidate discovery, so we hide these controls entirely until
    // the user clicks the root breadcrumb to return.
    const bool atNavigationRoot = state.navigationStack.size() <= 1;
    if (atNavigationRoot) {
        ImGui::SetNextItemWidth(160.0f);
        ImGui::Combo("Instance Source", &state.instanceSearchMode, INSTANCE_SEARCH_MODE_LABELS, IM_ARRAYSIZE(INSTANCE_SEARCH_MODE_LABELS));

        ImGui::SameLine();
        if (ImGui::Button("Find Instances", ImVec2(130, 0))) {
            if (!state.instanceSearchInProgress.load() && state.dumper && state.selectedClass) {
                if (state.instanceSearchMode == 0)
                    state.StartStaticInstanceSearch(state.dumper, state.selectedClass);
                else
                    state.StartLiveInstanceSearch(state.dumper, state.selectedClass);
            }
        }

        ImGui::SameLine();
        if (state.instanceSearchInProgress.load())
            ImGui::TextUnformatted("Searching...");
        else
            ImGui::TextDisabled("Select source + find");

        std::vector<void*> instances = inspectorSnapshot.instanceCandidates;
        int selectedIndex = state.selectedInstanceIndex;
        if (!instances.empty()) {
            if (selectedIndex < 0 || selectedIndex >= static_cast<int>(instances.size()))
                selectedIndex = 0;

            std::vector<std::string> labels;
            labels.reserve(instances.size());
            for (size_t i = 0; i < instances.size(); ++i) {
                char buffer[64] = {};
                snprintf(buffer, sizeof(buffer), "[%zu] %p", i, instances[i]);
                labels.emplace_back(buffer);
            }

            const char* preview = labels[selectedIndex].c_str();
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::BeginCombo("Instance", preview)) {
                for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
                    const bool isSelected = (i == selectedIndex);
                    if (ImGui::Selectable(labels[i].c_str(), isSelected)) {
                        state.SelectInstanceByIndex(i);
                        state.StartFieldsLoad(state.dumper, state.selectedClass);
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        else {
            ImGui::TextDisabled("No instance candidates");
        }
    }

    ImGui::Separator();
    ImGui::BeginChild("FieldsStatusBar", ImVec2(0, 25 * Config::GUI_SCALE), true, ImGuiWindowFlags_NoScrollbar);
    if (copyFeedback.copiedFieldAtSeconds > 0 && (static_cast<float>(ImGui::GetTime()) - copyFeedback.copiedFieldAtSeconds) < 2.0f) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Copied: %s", copyFeedback.copiedFieldOffset);
    }
    else if (state.fieldsLoadInProgress.load()) {
        ImGui::TextUnformatted("Refreshing fields...");
    }
    else if (editStatusAtSeconds > 0 && (static_cast<float>(ImGui::GetTime()) - editStatusAtSeconds) < 2.5f) {
        ImGui::TextUnformatted(editStatus);
    }
    else if (inspectorSnapshot.activeInstancePtr) {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Active: %p", inspectorSnapshot.activeInstancePtr);
        if (lastRefreshTimeStr[0] != '\0') {
            ImGui::SameLine();
            ImGui::TextDisabled("| %s: %s", lastRefreshLabel, lastRefreshTimeStr);
        }
    }
    else {
        ImGui::TextDisabled("Tip: Double-click Offset to copy");
    }
    ImGui::EndChild();

    ImGui::InputText("Filter Fields", state.fieldsFilterBuffer, sizeof(state.fieldsFilterBuffer));
    if (strcmp(state.fieldsCachedOriginalFilter.c_str(), state.fieldsFilterBuffer) != 0) {
        state.fieldsCachedOriginalFilter = state.fieldsFilterBuffer;
        state.fieldsCachedLowerFilter = Gui::Infra::SearchFilter::ToLowercase(state.fieldsFilterBuffer);
    }
    const bool fieldsFilterIsEmpty = state.fieldsCachedLowerFilter.empty();

    size_t visibleFieldCount = 0;
    if (ImGui::BeginTable("FieldsTable", 5,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("W", ImGuiTableColumnFlags_WidthFixed, 28.0f * Config::GUI_SCALE);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.32f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.22f);
        ImGui::TableHeadersRow();

        for (size_t fieldIndex = 0; fieldIndex < inspectorSnapshot.fields.size(); ++fieldIndex) {
            const auto& field = inspectorSnapshot.fields[fieldIndex];
            if (!fieldsFilterIsEmpty && !Gui::Infra::SearchFilter::FieldMatches(field, state.fieldsCachedLowerFilter)) {
                continue;
            }
            ++visibleFieldCount;

            const State::FieldDiffTint rowTint =
                state.fieldSnapshot.TintForField(State::FieldKeyFromInfo(field));
            ImGui::TableNextRow();
            if (rowTint != State::FieldDiffTint::None) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                        static_cast<ImU32>(State::FieldDiffTintToColor(rowTint)));
            }
            ImGui::PushID(static_cast<int>(fieldIndex));

            ImGui::TableSetColumnIndex(0);
            const bool watchable = state.dumper != nullptr && state.fieldWatch.IsWatchable(field);
            if (!watchable) {
                ImGui::BeginDisabled();
                ImGui::TextDisabled("-");
                ImGui::EndDisabled();
            }
            else {
                const bool watching = state.fieldWatch.IsWatching(state, field);
                const char* const watchLabel = watching ? "*" : "W";
                if (ImGui::Button(watchLabel, ImVec2(24.0f * Config::GUI_SCALE, 0))) {
                    const auto result = state.fieldWatch.Toggle(state, field);
                    if (result == State::FieldWatchModel::ToggleResult::RejectedCap) {
                        state.navigationFeedback.MarkStatus("Watchlist full (32)",
                                                              State::HistorySteadyNowSeconds());
                    }
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip(watching ? "Remove from watchlist" : "Add to watchlist");
                }
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(field.name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(field.type.c_str());
            ImGui::TableSetColumnIndex(3);

            char offsetBuffer[32] = {};
            snprintf(offsetBuffer, sizeof(offsetBuffer), "0x%zX", field.offset);

            if (ImGui::Selectable(offsetBuffer, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    ImGui::SetClipboardText(offsetBuffer);
                    strncpy_s(copyFeedback.copiedFieldOffset, sizeof(copyFeedback.copiedFieldOffset), offsetBuffer, _TRUNCATE);
                    copyFeedback.copiedFieldAtSeconds = static_cast<float>(ImGui::GetTime());
                }
            }

            ImGui::TableSetColumnIndex(4);
            const bool canEdit = inspectorSnapshot.activeInstancePtr != nullptr && field.hasValue && field.valueAddress != 0 && IsEditableFieldType(field.type);
            if (canEdit && state.dumper) {
                const uintptr_t key = field.valueAddress;
                auto& buffer = state.editBuffers[key];
                // Buffer was cleared by DoRefresh — always repopulate from fresh field data
                if (buffer[0] == '\0') {
                    const char* initialText = !field.valueDisplay.empty() ? field.valueDisplay.c_str() : "0";
                    strncpy_s(buffer.data(), buffer.size(), initialText, _TRUNCATE);
                }

                ImGui::PushID(static_cast<int>(fieldIndex) + 10000);
                const float availWidth = ImGui::GetContentRegionAvail().x;
                constexpr float applyBtnWidth = 28.0f;
                constexpr float spacing = 4.0f;
                ImGui::SetNextItemWidth(availWidth - applyBtnWidth - spacing);
                ImGui::InputText("##FieldEdit", buffer.data(), buffer.size());
                ImGui::SameLine(0.0f, spacing);
                if (ImGui::Button("OK", ImVec2(applyBtnWidth, 0))) {
                    std::string error;
                    if (state.dumper->SetFieldValue(field, buffer.data(), &error)) {
                        snprintf(editStatus, sizeof(editStatus), "Applied: %s", field.name.c_str());
                        editStatusAtSeconds = static_cast<float>(ImGui::GetTime());
                        State::FieldAuditPayload audit{};
                        audit.fieldName        = field.name;
                        audit.fieldType        = field.type;
                        audit.newValueDisplay  = buffer.data();
                        state.RecordFieldAudit(audit);
                        DoRefresh(false);
                    }
                    else {
                        snprintf(editStatus, sizeof(editStatus), "Edit failed: %s", error.empty() ? "Unknown error" : error.c_str());
                        editStatusAtSeconds = static_cast<float>(ImGui::GetTime());
                    }
                }
                ImGui::PopID();
            }
            else if (!field.valueDisplay.empty()) {
                // Pointer / array / list fields with a real (non-null,
                // non-error) value are rendered as "links" that drill into
                // the referenced object or collection. PTR -> a single
                // nested object; ARRAY / LIST -> a synthesized element list.
                // The valueDisplay sentinels filter out values we already
                // know won't decode.
                using Cat = Engine::Types::TypeCategory;
                const auto category = Engine::Types::GetCategory(field.type);
                const bool hasReadableValue = inspectorSnapshot.activeInstancePtr != nullptr
                    && field.hasValue
                    && field.valueAddress != 0
                    && field.valueDisplay != "null"
                    && field.valueDisplay != "??"
                    && field.valueDisplay != "[]";

                const bool isNavigablePtr        = hasReadableValue && category == Cat::PTR;
                const bool isNavigableCollection = hasReadableValue
                    && (category == Cat::ARRAY || category == Cat::LIST);

                if ((isNavigablePtr || isNavigableCollection) && state.dumper) {
                    ImGui::PushID(static_cast<int>(fieldIndex));
                    // Pointer drills get blue, collection drills get green
                    // so the user can distinguish "step into one object"
                    // from "browse N elements" at a glance.
                    const ImVec4 linkColor = isNavigableCollection
                        ? ImVec4(0.55f, 0.95f, 0.55f, 1.0f) // green
                        : ImVec4(0.55f, 0.75f, 1.0f, 1.0f); // blue
                    ImGui::PushStyleColor(ImGuiCol_Text, linkColor);
                    if (ImGui::SmallButton(field.valueDisplay.c_str())) {
                        bool navigated = false;
                        if (isNavigableCollection) {
                            navigated = state.NavigateIntoCollection(field);
                            if (!navigated) {
                                snprintf(editStatus, sizeof(editStatus),
                                         "Cannot open: %s collection header unreadable",
                                         field.name.c_str());
                                editStatusAtSeconds = static_cast<float>(ImGui::GetTime());
                            }
                        }
                        else {
                            navigated = state.NavigateIntoPointer(field.valueAddress, field.name);
                            if (!navigated) {
                                snprintf(editStatus, sizeof(editStatus),
                                         "Cannot navigate: %s isn't a managed object",
                                         field.name.c_str());
                                editStatusAtSeconds = static_cast<float>(ImGui::GetTime());
                            }
                        }
                    }
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                }
                else {
                    ImGui::TextUnformatted(field.valueDisplay.c_str());
                }
            }
            else if (field.valueAddress != 0 && field.hasValue) {
                char staticValueBuffer[32] = {};
                snprintf(staticValueBuffer, sizeof(staticValueBuffer), "0x%llX", static_cast<unsigned long long>(field.valueAddress));
                ImGui::TextUnformatted(staticValueBuffer);
            }
            else {
                ImGui::TextUnformatted("-");
            }

            ImGui::PopID();
        }

        ImGui::EndTable();

        if (!fieldsFilterIsEmpty && visibleFieldCount == 0) {
            ImGui::TextDisabled("No fields match filter.");
        }
    }

    RenderTwoInstanceCompareModal(state, inspectorSnapshot);
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
