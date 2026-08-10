#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/fields/field_compare_modal.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"
#include "gui/state/fields/field_path_resolver.h"
#include "gui/state/fields/field_snapshot_model.h"
#include "gui/views/sidebar/class_label_lookup.h"

#include "services/main_thread_dispatcher.h"

#include "imgui.h"

#include <cstdio>
#include <mutex>
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

struct PathCompareSnapshot {
    bool        hasPathResult = false;
    std::string pathLabel{};
    std::string pathValueA{};
    std::string pathValueB{};
    std::string pathErrorA{};
    std::string pathErrorB{};
    bool        pathDiffers = false;
};

struct TwoInstanceCompareState {
    int instanceIndexA = 0;
    int instanceIndexB = 1;
    int compareMode = static_cast<int>(TwoInstanceCompareMode::ClassFields);
    int collectionElementIndex = 0;
    int selectedLeafFieldIndex = 0;

    // Result fields — written by compareThread, read by GUI under mutex.
    std::mutex mutex{};
    bool hasCompareResult = false;
    std::vector<TwoInstanceCompareRow> rows{};
    PathCompareSnapshot path{};
    std::string workerError{};
};

TwoInstanceCompareState& CompareState() {
    static TwoInstanceCompareState s_state;
    return s_state;
}

std::string LookupSidebarClassName(const ControlPanelSessionState& state) {
    if (!state.selectedClass) {
        return {};
    }
    const std::string label = LookupClassDisplayName(state, state.selectedClass);
    return label.empty() ? std::string("<class>") : label;
}

std::string InstanceLabel(const std::vector<void*>& instances, int index) {
    char buffer[64] = {};
    snprintf(buffer, sizeof(buffer), "[%d] %p", index, instances[static_cast<size_t>(index)]);
    return buffer;
}

void ResetResultsLocked(TwoInstanceCompareState& modal) {
    modal.hasCompareResult = false;
    modal.rows.clear();
    modal.path = {};
    modal.workerError.clear();
}

std::vector<TwoInstanceCompareRow> BuildClassCompareRows(Engine::UnityDumper& dumper,
                                                         void* klass,
                                                         void* instanceA,
                                                         void* instanceB) {
    const auto fieldsA = dumper.GetRawFields(klass, instanceA);
    const auto fieldsB = dumper.GetRawFields(klass, instanceB);

    std::unordered_map<std::string, Engine::FieldInfo> byNameB;
    byNameB.reserve(fieldsB.size());
    for (const auto& field : fieldsB) {
        byNameB[field.name] = field;
    }

    std::vector<TwoInstanceCompareRow> rows;
    rows.reserve(fieldsA.size() + byNameB.size());
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
        rows.push_back(std::move(row));
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
        rows.push_back(std::move(row));
    }

    return rows;
}

PathCompareSnapshot BuildPathCompareSnapshot(Engine::UnityDumper& dumper,
                                             void* sidebarKlass,
                                             void* instanceA,
                                             void* instanceB,
                                             const std::vector<State::FieldPathStep>& steps,
                                             int collectionIndex,
                                             const std::string& leafFieldName,
                                             const std::string& pathLabel) {
    PathCompareSnapshot out{};
    out.pathLabel = pathLabel;

    const State::FieldPathCompareResult resultA =
        State::ResolvePathValue(dumper, sidebarKlass, instanceA, steps, collectionIndex, leafFieldName);
    const State::FieldPathCompareResult resultB =
        State::ResolvePathValue(dumper, sidebarKlass, instanceB, steps, collectionIndex, leafFieldName);

    out.pathValueA = resultA.ok
        ? (resultA.valueDisplay.empty() ? std::string("-") : resultA.valueDisplay)
        : std::string("-");
    out.pathValueB = resultB.ok
        ? (resultB.valueDisplay.empty() ? std::string("-") : resultB.valueDisplay)
        : std::string("-");
    if (!resultA.ok) {
        out.pathErrorA = resultA.error;
    }
    if (!resultB.ok) {
        out.pathErrorB = resultB.error;
    }
    out.pathDiffers = resultA.ok && resultB.ok && out.pathValueA != out.pathValueB;
    out.hasPathResult = true;
    return out;
}

void StartAsyncCompare(ControlPanelSessionState& state,
                       TwoInstanceCompareState& modal,
                       bool classMode,
                       void* instanceA,
                       void* instanceB,
                       const InspectorCache& inspectorSnapshot,
                       bool inCollectionView) {
    auto dumperRef = state.dumper;
    void* const klass = state.selectedClass;
    if (!dumperRef || !klass || !instanceA || !instanceB) {
        return;
    }

    std::vector<State::FieldPathStep> steps;
    std::string leafFieldName{};
    std::string pathLabel{};
    int collectionIndex = -1;
    if (!classMode) {
        steps = State::BuildFieldPathSteps(state.walker.stack);
        if (!inCollectionView && !inspectorSnapshot.fields.empty()
            && modal.selectedLeafFieldIndex >= 0
            && modal.selectedLeafFieldIndex < static_cast<int>(inspectorSnapshot.fields.size())) {
            leafFieldName = inspectorSnapshot.fields[static_cast<size_t>(modal.selectedLeafFieldIndex)].name;
        }
        collectionIndex = inCollectionView ? modal.collectionElementIndex : -1;
        pathLabel = State::BuildFieldPathLabel(state.walker.stack, collectionIndex);
    }

    {
        std::lock_guard<std::mutex> lock(modal.mutex);
        ResetResultsLocked(modal);
    }

    state.loaders.compareThread = {};
    state.loaders.compareInProgress.store(true);

    TwoInstanceCompareState* const modalPtr = &modal;
    state.loaders.compareThread = std::jthread(
        [dumperRef, klass, instanceA, instanceB, classMode, steps = std::move(steps),
         leafFieldName = std::move(leafFieldName), pathLabel = std::move(pathLabel),
         collectionIndex, modalPtr,
         &inProgress = state.loaders.compareInProgress](std::stop_token stopToken) {
            Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
            try {
                if (classMode) {
                    auto rows = BuildClassCompareRows(*dumperRef, klass, instanceA, instanceB);
                    if (stopToken.stop_requested()) {
                        inProgress.store(false);
                        return;
                    }
                    std::lock_guard<std::mutex> lock(modalPtr->mutex);
                    modalPtr->rows = std::move(rows);
                    modalPtr->hasCompareResult = true;
                    modalPtr->path = {};
                    modalPtr->workerError.clear();
                }
                else {
                    auto path = BuildPathCompareSnapshot(
                        *dumperRef, klass, instanceA, instanceB, steps, collectionIndex,
                        leafFieldName, pathLabel);
                    if (stopToken.stop_requested()) {
                        inProgress.store(false);
                        return;
                    }
                    std::lock_guard<std::mutex> lock(modalPtr->mutex);
                    modalPtr->path = std::move(path);
                    modalPtr->hasCompareResult = false;
                    modalPtr->rows.clear();
                    modalPtr->workerError.clear();
                }
            }
            catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(modalPtr->mutex);
                ResetResultsLocked(*modalPtr);
                modalPtr->workerError = e.what();
            }
            catch (...) {
                std::lock_guard<std::mutex> lock(modalPtr->mutex);
                ResetResultsLocked(*modalPtr);
                modalPtr->workerError = "Compare failed (unknown error)";
            }
            inProgress.store(false);
        });
}
} // namespace

void RenderTwoInstanceComparePanel(ControlPanelSessionState& state,
                                   const InspectorCache& inspectorSnapshot) {
    auto& modal = CompareState();

    const std::vector<void*> instances = state.inspector.SnapshotRootInstanceCandidates();
    const bool inCollectionView = !state.walker.stack.empty() && state.walker.stack.back().isCollection;
    const bool pathContextAvailable = state.walker.stack.size() > 1;
    const bool compareBusy = state.loaders.compareInProgress.load(std::memory_order_relaxed);

    if (instances.size() < 2 || !state.selectedClass || !state.dumper) {
        ImGui::TextDisabled(
            "Need at least two root instance candidates (use Find Instances at class root).");
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
        ImGui::TextDisabled("Path: %s",
            State::BuildFieldPathLabel(
                state.walker.stack,
                inCollectionView ? modal.collectionElementIndex : -1).c_str());

        if (inCollectionView) {
            const int elementCount = static_cast<int>(inspectorSnapshot.fields.size());
            if (elementCount <= 0) {
                UiTheme::DrawErrorText("Collection has no elements to compare.");
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
            UiTheme::DrawErrorText("No fields loaded for the current object.");
        }
        ImGui::Spacing();
    }
    else if (modal.compareMode == classMode && state.walker.stack.size() > 1) {
        const std::string classLabel = LookupSidebarClassName(state);
        char msg[256] = {};
        std::snprintf(msg, sizeof(msg),
                      "Class fields mode compares declared fields of %s.",
                      classLabel.empty() ? "<class>" : classLabel.c_str());
        UiTheme::DrawWarningText(msg);
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

    ImGui::SameLine();
    ImGui::SetNextItemWidth(280.0f);
    if (ImGui::BeginCombo("Instance B", InstanceLabel(instances, modal.instanceIndexB).c_str())) {
        for (int i = 0; i < static_cast<int>(instances.size()); ++i) {
            if (ImGui::Selectable(InstanceLabel(instances, i).c_str(), i == modal.instanceIndexB)) {
                modal.instanceIndexB = i;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    const bool runEnabled = !compareBusy
        && modal.instanceIndexA != modal.instanceIndexB
        && (modal.compareMode != pathMode || (pathContextAvailable && (!inCollectionView || !inspectorSnapshot.fields.empty())));

    if (!runEnabled) {
        ImGui::BeginDisabled();
    }
    if (UiTheme::PrimaryButton("Run compare")) {
        if (modal.instanceIndexA != modal.instanceIndexB) {
            void* const instanceA = instances[static_cast<size_t>(modal.instanceIndexA)];
            void* const instanceB = instances[static_cast<size_t>(modal.instanceIndexB)];
            StartAsyncCompare(state, modal, modal.compareMode == classMode,
                              instanceA, instanceB, inspectorSnapshot, inCollectionView);
        }
    }
    if (!runEnabled) {
        ImGui::EndDisabled();
    }

    if (compareBusy) {
        ImGui::SameLine();
        ImGui::TextDisabled("Comparing...");
    }

    bool hasCompareResult = false;
    bool hasPathResult = false;
    std::vector<TwoInstanceCompareRow> rowsCopy;
    PathCompareSnapshot pathCopy{};
    std::string workerError{};
    {
        std::lock_guard<std::mutex> lock(modal.mutex);
        hasCompareResult = modal.hasCompareResult;
        hasPathResult = modal.path.hasPathResult;
        rowsCopy = modal.rows;
        pathCopy = modal.path;
        workerError = modal.workerError;
    }

    if (!workerError.empty()) {
        ImGui::Separator();
        UiTheme::DrawErrorText(workerError.c_str());
    }

    if (modal.compareMode == classMode && hasCompareResult && !rowsCopy.empty()) {
        ImGui::Separator();
        if (ImGui::BeginChild("TwoInstanceCompareResults", ImVec2(0, 0), false)) {
            if (ImGui::BeginTable("TwoInstanceCompareTable", 4,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                      | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable
                                      | ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.28f);
                ImGui::TableSetupColumn("Value A", ImGuiTableColumnFlags_WidthStretch, 0.28f);
                ImGui::TableSetupColumn("Value B", ImGuiTableColumnFlags_WidthStretch, 0.28f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.16f);
                ImGui::TableHeadersRow();

                for (const auto& row : rowsCopy) {
                    ImGui::TableNextRow();
                    if (row.differs) {
                        ImGui::TableSetBgColor(
                            ImGuiTableBgTarget_RowBg0,
                            static_cast<ImU32>(State::FieldDiffTintToColor(State::FieldDiffTint::Changed)));
                    }
                    ImGui::TableSetColumnIndex(0);
                    UiTheme::DrawColumnName(row.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    UiTheme::DrawColumnValue(row.valueA.c_str());
                    ImGui::TableSetColumnIndex(2);
                    UiTheme::DrawColumnValue(row.valueB.c_str());
                    ImGui::TableSetColumnIndex(3);
                    UiTheme::DrawColumnType(row.type.c_str());
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }
    else if (modal.compareMode == pathMode && hasPathResult) {
        ImGui::Separator();
        if (ImGui::BeginTable("TwoInstancePathCompare", 2,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                  | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Instance", ImGuiTableColumnFlags_WidthStretch, 0.30f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.70f);
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
                UiTheme::DrawColumnName(label);
                ImGui::TableSetColumnIndex(1);
                if (!error.empty()) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(UiTheme::Tokens().error, "%s", error.c_str());
                }
                else {
                    UiTheme::DrawColumnValue(value.c_str());
                }
            };

            drawPathRow("A", pathCopy.pathValueA, pathCopy.pathErrorA, pathCopy.pathDiffers);
            drawPathRow("B", pathCopy.pathValueB, pathCopy.pathErrorB, pathCopy.pathDiffers);
            ImGui::EndTable();
        }
    }
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
