#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/fields/field_compare_modal.h"

#include "gui/session_state.h"
#include "gui/state/fields/field_path_resolver.h"
#include "gui/state/fields/field_snapshot_model.h"

#include "imgui.h"

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace Gui::Views
{
namespace
{
struct TwoInstanceCompareRow {
    std::string name{};
    std::string type{};
    std::string valueA{};
    std::string valueB{};
    bool        differs = false;
};

enum class TwoInstanceCompareMode {
    ClassFields = 0,
    CurrentPath = 1,
};

struct TwoInstanceModalState {
    bool openRequested = false;
    int instanceIndexA = 0;
    int instanceIndexB = 1;
    int compareMode = static_cast<int>(TwoInstanceCompareMode::ClassFields);
    int collectionElementIndex = 0;
    int selectedLeafFieldIndex = 0;
    bool hasCompareResult = false;
    std::vector<TwoInstanceCompareRow> rows{};
    bool hasPathResult = false;
    std::string pathLabel{};
    std::string pathValueA{};
    std::string pathValueB{};
    std::string pathErrorA{};
    std::string pathErrorB{};
    bool pathDiffers = false;
};

TwoInstanceModalState& TwoInstanceModal() {
    static TwoInstanceModalState s_state;
    return s_state;
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

std::string InstanceLabel(const std::vector<void*>& instances, int index) {
    char buffer[64] = {};
    snprintf(buffer, sizeof(buffer), "[%d] %p", index, instances[static_cast<size_t>(index)]);
    return buffer;
}

void ResetResults(TwoInstanceModalState& modal) {
    modal.hasCompareResult = false;
    modal.hasPathResult = false;
    modal.rows.clear();
}

void BuildClassCompareRows(TwoInstanceModalState& modal,
                           ControlPanelSessionState& state,
                           void* instanceA,
                           void* instanceB) {
    const auto fieldsA = state.dumper->GetRawFields(state.selectedClass, instanceA);
    const auto fieldsB = state.dumper->GetRawFields(state.selectedClass, instanceB);

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
        row.valueA = fieldA.valueDisplay.empty() ? std::string("-") : fieldA.valueDisplay;

        const auto itB = byNameB.find(fieldA.name);
        if (itB == byNameB.end()) {
            row.valueB = "-";
            row.differs = true;
        }
        else {
            row.valueB = itB->second.valueDisplay.empty() ? std::string("-") : itB->second.valueDisplay;
            row.differs = row.valueA != row.valueB;
        }
        modal.rows.push_back(std::move(row));
    }

    for (const auto& fieldB : fieldsB) {
        if (seen[fieldB.name]) {
            continue;
        }
        TwoInstanceCompareRow row{};
        row.name = fieldB.name;
        row.type = fieldB.type;
        row.valueA = "-";
        row.valueB = fieldB.valueDisplay.empty() ? std::string("-") : fieldB.valueDisplay;
        row.differs = true;
        modal.rows.push_back(std::move(row));
    }

    modal.hasCompareResult = true;
}

void BuildPathCompareRows(TwoInstanceModalState& modal,
                          ControlPanelSessionState& state,
                          const InspectorCache& inspectorSnapshot,
                          bool inCollectionView,
                          void* instanceA,
                          void* instanceB) {
    const auto steps = State::BuildFieldPathSteps(state.walker.stack);
    std::string leafFieldName{};
    if (!inCollectionView && !inspectorSnapshot.fields.empty()
        && modal.selectedLeafFieldIndex >= 0
        && modal.selectedLeafFieldIndex < static_cast<int>(inspectorSnapshot.fields.size())) {
        leafFieldName = inspectorSnapshot.fields[static_cast<size_t>(modal.selectedLeafFieldIndex)].name;
    }

    const int collectionIndex = inCollectionView ? modal.collectionElementIndex : -1;
    modal.pathLabel = State::BuildFieldPathLabel(state.walker.stack, collectionIndex);

    const State::FieldPathCompareResult resultA =
        State::ResolvePathValue(*state.dumper, state.selectedClass, instanceA, steps, collectionIndex, leafFieldName);
    const State::FieldPathCompareResult resultB =
        State::ResolvePathValue(*state.dumper, state.selectedClass, instanceB, steps, collectionIndex, leafFieldName);

    modal.pathErrorA.clear();
    modal.pathErrorB.clear();
    modal.pathValueA = resultA.ok
        ? (resultA.valueDisplay.empty() ? std::string("-") : resultA.valueDisplay)
        : std::string("-");
    modal.pathValueB = resultB.ok
        ? (resultB.valueDisplay.empty() ? std::string("-") : resultB.valueDisplay)
        : std::string("-");
    if (!resultA.ok) {
        modal.pathErrorA = resultA.error;
    }
    if (!resultB.ok) {
        modal.pathErrorB = resultB.error;
    }
    modal.pathDiffers = resultA.ok && resultB.ok && modal.pathValueA != modal.pathValueB;
    modal.hasPathResult = true;
}
} // namespace

void RequestOpenTwoInstanceCompareModal() {
    TwoInstanceModal().openRequested = true;
}

void RenderTwoInstanceCompareModal(ControlPanelSessionState& state,
                                   const InspectorCache& inspectorSnapshot) {
    auto& modal = TwoInstanceModal();
    if (modal.openRequested) {
        ImGui::OpenPopup("CompareTwoInstancesPopup");
        modal.openRequested = false;
    }

    if (!ImGui::BeginPopupModal("CompareTwoInstancesPopup", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    const std::vector<void*>& instances = state.inspector.rootInstanceCandidates;
    const bool inCollectionView = !state.walker.stack.empty() && state.walker.stack.back().isCollection;
    const bool pathContextAvailable = state.walker.stack.size() > 1;

    if (instances.size() < 2 || !state.selectedClass || !state.dumper) {
        ImGui::TextUnformatted("Need at least two root instance candidates (use Find Instances at class root).");
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            ResetResults(modal);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
    }

    if (inCollectionView && modal.compareMode == static_cast<int>(TwoInstanceCompareMode::ClassFields)) {
        modal.compareMode = static_cast<int>(TwoInstanceCompareMode::CurrentPath);
    }

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
    const int pathMode = static_cast<int>(TwoInstanceCompareMode::CurrentPath);

    if (inCollectionView) {
        ImGui::BeginDisabled();
    }
    ImGui::RadioButton("Class fields", &modal.compareMode, classMode);
    if (inCollectionView) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Class-field compare is only available outside a collection view. Use Current path.");
        }
    }

    ImGui::SameLine();
    if (!pathContextAvailable) {
        ImGui::BeginDisabled();
    }
    ImGui::RadioButton("Current path", &modal.compareMode, pathMode);
    if (!pathContextAvailable) {
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Drill into a member field first.");
        }
    }

    if (modal.compareMode == pathMode && pathContextAvailable) {
        modal.pathLabel = State::BuildFieldPathLabel(
            state.walker.stack,
            inCollectionView ? modal.collectionElementIndex : -1);
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
                ImGui::SliderInt("Element index", &modal.collectionElementIndex, 0, elementCount - 1);
            }
        }
        else if (!inspectorSnapshot.fields.empty()) {
            if (modal.selectedLeafFieldIndex < 0
                || modal.selectedLeafFieldIndex >= static_cast<int>(inspectorSnapshot.fields.size())) {
                modal.selectedLeafFieldIndex = 0;
            }
            const auto& leafField = inspectorSnapshot.fields[static_cast<size_t>(modal.selectedLeafFieldIndex)];
            ImGui::SetNextItemWidth(280.0f);
            if (ImGui::BeginCombo("Member field", leafField.name.c_str())) {
                for (int i = 0; i < static_cast<int>(inspectorSnapshot.fields.size()); ++i) {
                    const bool selected = (i == modal.selectedLeafFieldIndex);
                    if (ImGui::Selectable(inspectorSnapshot.fields[static_cast<size_t>(i)].name.c_str(), selected)) {
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
    else if (modal.compareMode == classMode && state.walker.stack.size() > 1) {
        const std::string classLabel = LookupSidebarClassName(state);
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.35f, 1.0f),
                           "Class fields mode compares declared fields of %s.",
                           classLabel.empty() ? "<class>" : classLabel.c_str());
        ImGui::Spacing();
    }

    ImGui::SetNextItemWidth(280.0f);
    if (ImGui::BeginCombo("Instance A", InstanceLabel(instances, modal.instanceIndexA).c_str())) {
        for (int i = 0; i < static_cast<int>(instances.size()); ++i) {
            if (ImGui::Selectable(InstanceLabel(instances, i).c_str(), i == modal.instanceIndexA)) {
                modal.instanceIndexA = i;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SetNextItemWidth(280.0f);
    if (ImGui::BeginCombo("Instance B", InstanceLabel(instances, modal.instanceIndexB).c_str())) {
        for (int i = 0; i < static_cast<int>(instances.size()); ++i) {
            if (ImGui::Selectable(InstanceLabel(instances, i).c_str(), i == modal.instanceIndexB)) {
                modal.instanceIndexB = i;
            }
        }
        ImGui::EndCombo();
    }

    const bool runEnabled = modal.instanceIndexA != modal.instanceIndexB
        && (modal.compareMode != pathMode || (pathContextAvailable && (!inCollectionView || !inspectorSnapshot.fields.empty())));

    if (!runEnabled) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Run compare", ImVec2(120, 0))) {
        ResetResults(modal);
        if (modal.instanceIndexA != modal.instanceIndexB) {
            void* const instanceA = instances[static_cast<size_t>(modal.instanceIndexA)];
            void* const instanceB = instances[static_cast<size_t>(modal.instanceIndexB)];
            if (modal.compareMode == classMode) {
                BuildClassCompareRows(modal, state, instanceA, instanceB);
            }
            else {
                BuildPathCompareRows(modal, state, inspectorSnapshot, inCollectionView, instanceA, instanceB);
            }
        }
    }
    if (!runEnabled) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(120, 0))) {
        ResetResults(modal);
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
                        static_cast<ImU32>(State::FieldDiffTintToColor(State::FieldDiffTint::Changed)));
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
                        static_cast<ImU32>(State::FieldDiffTintToColor(State::FieldDiffTint::Changed)));
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
} // namespace Gui::Views

#endif // ENABLE_DUMPER
