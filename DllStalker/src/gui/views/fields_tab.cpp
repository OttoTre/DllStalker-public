#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/fields_tab.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"
#include "gui/config.h"
#include "gui/infra/search_filter.h"
#include "gui/state/fields/field_path_resolver.h"
#include "gui/state/fields/field_snapshot_model.h"
#include "gui/state/fields/field_watch_model.h"
#include "gui/state/navigation/history_steady_time.h"
#include "gui/views/fields/field_edit_cells.h"
#include "types/memory_guard.h"
#include "types/type_classifier.h"

#include "imgui.h"

#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace Gui::Views
{
namespace
{
constexpr float FIELD_REFRESH_INTERVALS[] = { 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };
constexpr const char* FIELD_REFRESH_INTERVAL_LABELS[] = { "0.5s", "1s", "2s", "3s", "4s", "5s" };
constexpr const char* INSTANCE_SEARCH_MODE_LABELS[] = { "Static discovery", "Live API" };

// Single source of truth: scalars, booleans, and System.String are editable.
// Excluded categories:
//   PTR           — overwriting a managed reference is unsafe and would
//                   bypass GC bookkeeping
//   ARRAY / LIST  — these are navigation targets (open in the Walker),
//                   not values to overwrite. Falling into the edit
//                   branch would also hide the green drill-in link.
//   UNKNOWN       — we don't know how to parse it
bool IsTransformRelatedField(const Engine::FieldInfo& field, ControlPanelSessionState& state) {
    using Cat = Engine::Types::TypeCategory;
    if (Engine::Types::GetCategory(field.type) != Cat::PTR) {
        return false;
    }
    if (!field.hasValue || field.valueAddress == 0 || !state.dumper) {
        return false;
    }
    void* ptr = nullptr;
    if (!Engine::Memory::TryReadValue(field.valueAddress, ptr) || !ptr) {
        return false;
    }
    void* klass = nullptr;
    state.dumper->TryGetClassNameFromInstance(ptr, &klass);
    if (!klass) {
        return false;
    }
    return state.dumper->IsOrInheritsFrom(klass, "Transform")
        || state.dumper->IsOrInheritsFrom(klass, "GameObject");
}

} // namespace

void RenderFieldsTab(const InspectorCache& inspectorSnapshot,
                     CopyFeedbackState& copyFeedback,
                     bool inspectorLoadInProgress,
                     ControlPanelSessionState& state) {
    static char editStatus[128] = {};
    static float editStatusAtSeconds = -1000.0f;
    static char lastRefreshTimeStr[32] = {};
    static char lastRefreshLabel[16] = {};

    const bool inCollectionViewEarly = !state.walker.stack.empty()
                                    && state.walker.stack.back().isCollection;
    const bool fieldsBusy = state.loaders.fieldsLoadInProgress.load()
                         || state.loaders.inspectorLoadInProgress.load();

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
        ? state.walker.stack.back().sourceField
        : Engine::FieldInfo{};

    auto DoRefresh = [&](bool isManual) {
        if (state.loaders.fieldsLoadInProgress.load() || state.loaders.inspectorLoadInProgress.load()
            || !state.dumper || !state.selectedClass) {
            return;
        }
        state.editBufferStore.buffers.clear();
        state.enumLiteralCache.Clear();
        state.fieldsLastRefreshAt = ImGui::GetTime();
        if (inCollectionView) {
            // Retrieve owner context from the active breadcrumb so the cache
            // entries remain stable after refresh (same klass/instance pair
            // that was set during the initial NavigateIntoCollection call).
            void* ownerKlass    = state.walker.stack.back().klass;
            void* ownerInstance = state.walker.stack.back().instance;
            state.StartCollectionLoad(state.dumper, collectionSource, ownerKlass, ownerInstance);
        }
        else {
            state.StartFieldsLoad(state.dumper, state.selectedClass);
        }
        WriteTimestamp(lastRefreshTimeStr, sizeof(lastRefreshTimeStr));
        strncpy_s(lastRefreshLabel, sizeof(lastRefreshLabel), isManual ? "Manual" : "Auto", _TRUNCATE);
    };

    if (inspectorSnapshot.fields.empty() && !inspectorLoadInProgress && !state.loaders.fieldsLoadInProgress.load()) {
        ImGui::TextUnformatted("No fields available.");
    }

    if (UiTheme::IconRefreshButton("##refresh_fields", "Refresh fields")) {
        DoRefresh(true);
    }

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    UiTheme::ChipToggle("Auto", &state.fieldsAutoRefresh);

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(90.0f);
    ImGui::Combo("##FieldsRefreshInterval", &state.fieldsRefreshIntervalIndex, FIELD_REFRESH_INTERVAL_LABELS, IM_ARRAYSIZE(FIELD_REFRESH_INTERVAL_LABELS));

    if (state.fieldsAutoRefresh && state.dumper && state.selectedClass
        && !state.loaders.fieldsLoadInProgress.load()
        && !(inCollectionView && state.loaders.inspectorLoadInProgress.load())) {
        const int intervalIndex = (state.fieldsRefreshIntervalIndex >= 0 && state.fieldsRefreshIntervalIndex < IM_ARRAYSIZE(FIELD_REFRESH_INTERVALS))
            ? state.fieldsRefreshIntervalIndex
            : 1;

        const double now = ImGui::GetTime();
        const double intervalSeconds = static_cast<double>(FIELD_REFRESH_INTERVALS[intervalIndex]);
        if (state.fieldsLastRefreshAt <= 0.0 || (now - state.fieldsLastRefreshAt) >= intervalSeconds) {
            DoRefresh(false);
        }
    }

    // Keep diff tints live on Fields when Analysis "Show changes" is on.
    if (state.fieldSnapshot.showChanges && state.fieldSnapshot.HasBaseline() && !fieldsBusy) {
        state.fieldSnapshot.RecomputeDiff(inspectorSnapshot.fields);
    }

    // The candidate-source picker and the candidates combobox only make sense
    // at the navigation root. While the user is drilled into a nested object
    // (breadcrumb depth > 1) the active instance is determined by the walker,
    // not by candidate discovery, so we hide these controls entirely until
    // the user clicks the root breadcrumb to return.
    const bool atNavigationRoot = state.walker.stack.size() <= 1;
    if (atNavigationRoot) {
        ImGui::SetNextItemWidth(220.0f);
        ImGui::Combo("##instance_source", &state.inspector.instanceSearchMode, INSTANCE_SEARCH_MODE_LABELS, IM_ARRAYSIZE(INSTANCE_SEARCH_MODE_LABELS));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Instance source: Static discovery or Live API");
        }

        ImGui::SameLine();
        const bool searchBusy = state.loaders.instanceSearchInProgress.load();
        if (searchBusy) {
            ImGui::BeginDisabled();
        }
        if (UiTheme::IconSearchButton("##find_instances", "Find instances") && !searchBusy) {
            if (state.dumper && state.selectedClass) {
                if (state.inspector.instanceSearchMode == 0)
                    state.StartStaticInstanceSearch(state.dumper, state.selectedClass);
                else
                    state.StartLiveInstanceSearch(state.dumper, state.selectedClass);
            }
        }
        if (searchBusy) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (searchBusy) {
            ImGui::TextUnformatted("Searching...");
        }

        std::vector<void*> instances = inspectorSnapshot.instanceCandidates;
        int selectedIndex = state.inspector.selectedInstanceIndex;
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
            ImGui::SameLine();
            ImGui::SetNextItemWidth(260.0f);
            if (ImGui::BeginCombo("##instance_pick", preview)) {
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
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Active instance");
            }
        }
        else if (!searchBusy) {
            ImGui::SameLine();
            ImGui::TextDisabled("No instance candidates");
        }
    }

    ImGui::Separator();
    if (copyFeedback.copiedFieldAtSeconds > 0 && (static_cast<float>(ImGui::GetTime()) - copyFeedback.copiedFieldAtSeconds) < 2.0f) {
        char msg[96] = {};
        std::snprintf(msg, sizeof(msg), "Copied: %s", copyFeedback.copiedFieldOffset);
        UiTheme::DrawSuccessText(msg);
    }
    else if (state.loaders.fieldsLoadInProgress.load()) {
        ImGui::TextUnformatted("Refreshing fields...");
    }
    else if (editStatusAtSeconds > 0 && (static_cast<float>(ImGui::GetTime()) - editStatusAtSeconds) < 2.5f) {
        ImGui::TextUnformatted(editStatus);
    }
    else if (inspectorSnapshot.activeInstancePtr) {
        char msg[64] = {};
        std::snprintf(msg, sizeof(msg), "Active: %p", inspectorSnapshot.activeInstancePtr);
        UiTheme::DrawSuccessText(msg);
        if (lastRefreshTimeStr[0] != '\0') {
            ImGui::SameLine();
            ImGui::TextDisabled("| %s: %s", lastRefreshLabel, lastRefreshTimeStr);
        }
    }
    else {
        ImGui::TextDisabled("Tip: Double-click Offset to copy");
    }

    UiTheme::ElevatedFilter("##fields_filter", state.fieldsFilterBuffer, sizeof(state.fieldsFilterBuffer),
                            -1.0f, "Filter...");
    if (strcmp(state.fieldsCachedOriginalFilter.c_str(), state.fieldsFilterBuffer) != 0) {
        state.fieldsCachedOriginalFilter = state.fieldsFilterBuffer;
        state.fieldsCachedLowerFilter = Gui::Infra::SearchFilter::ToLowercase(state.fieldsFilterBuffer);
    }
    const bool fieldsFilterIsEmpty = state.fieldsCachedLowerFilter.empty();

    size_t visibleFieldCount = 0;
    const bool skipEmptyLoadingTable =
        inspectorLoadInProgress && inspectorSnapshot.fields.empty();
    if (skipEmptyLoadingTable) {
        ImGui::TextDisabled("Waiting for fields...");
    }
    else if (UiTheme::BeginInspectorTable("FieldsTable", 5)) {
        const float glyphColW = 28.0f * Config::GUI_SCALE;
        ImGui::TableSetupColumn("##W", ImGuiTableColumnFlags_WidthFixed, glyphColW);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.16f);
        ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 72.0f * Config::GUI_SCALE);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.42f);

        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableSetColumnIndex(0);
        {
            ImGui::TableHeader("##W");
            const ImVec2 rmin = ImGui::GetItemRectMin();
            const ImVec2 rmax = ImGui::GetItemRectMax();
            const ImVec2 textSize = ImGui::CalcTextSize("W");
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(rmin.x + (rmax.x - rmin.x - textSize.x) * 0.5f,
                       rmin.y + (rmax.y - rmin.y - textSize.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), "W");
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::TableHeader("Name");
        ImGui::TableSetColumnIndex(2);
        ImGui::TableHeader("Type");
        ImGui::TableSetColumnIndex(3);
        ImGui::TableHeader("Offset");
        ImGui::TableSetColumnIndex(4);
        ImGui::TableHeader("Value");

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
                UiTheme::CenteredDisabledGlyph("-", glyphColW);
            }
            else {
                const bool watching = state.fieldWatch.IsWatching(state, field);
                const char* const watchLabel = watching ? "*" : "W";
                const ImVec4* watchColor = watching ? &UiTheme::Tokens().error : nullptr;
                if (UiTheme::CenteredGlyphButton("##watch", watchLabel, glyphColW, watchColor)) {
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
            UiTheme::DrawColumnName(field.name.c_str());
            ImGui::TableSetColumnIndex(2);
            UiTheme::DrawColumnType(field.type.c_str());
            ImGui::TableSetColumnIndex(3);

            char offsetBuffer[32] = {};
            snprintf(offsetBuffer, sizeof(offsetBuffer), "0x%zX", field.offset);

            ImGui::PushStyleColor(ImGuiCol_Text, UiTheme::Tokens().semantic_link);
            ImGui::AlignTextToFramePadding();
            if (ImGui::Selectable(offsetBuffer, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    ImGui::SetClipboardText(offsetBuffer);
                    strncpy_s(copyFeedback.copiedFieldOffset, sizeof(copyFeedback.copiedFieldOffset), offsetBuffer, _TRUNCATE);
                    copyFeedback.copiedFieldAtSeconds = static_cast<float>(ImGui::GetTime());
                }
            }
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(4);
            const bool haveFieldTarget = inspectorSnapshot.activeInstancePtr != nullptr
                                      || field.staticValue != 0;
            const bool canEdit = haveFieldTarget && field.hasValue && field.valueAddress != 0
                              && IsEditableFieldType(field);
            if (canEdit && state.dumper) {
                if (field.isEnum) {
                    RenderEnumFieldEditCell(field, state, fieldIndex, editStatus, editStatusAtSeconds, DoRefresh);
                }
                else {
                    RenderScalarFieldEditCell(field, state, fieldIndex, editStatus, editStatusAtSeconds, DoRefresh);
                }
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
                    && field.valueDisplay != "[null]"
                    && field.valueDisplay != "??"
                    && field.valueDisplay != "[]";

                const bool isNavigablePtr        = hasReadableValue && category == Cat::PTR && !field.isEnum;
                const bool isNavigableCollection = hasReadableValue
                    && (category == Cat::ARRAY || category == Cat::LIST);

                if ((isNavigablePtr || isNavigableCollection) && state.dumper) {
                    ImGui::PushID(static_cast<int>(fieldIndex));
                    // Pointer drills get blue, collection drills get green
                    // so the user can distinguish "step into one object"
                    // from "browse N elements" at a glance.
                    const ImVec4 linkColor = isNavigableCollection
                        ? UiTheme::Tokens().semantic_struct
                        : UiTheme::Tokens().semantic_link;
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
                    if (isNavigablePtr && IsTransformRelatedField(field, state)) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("[T]")) {
                            void* target = nullptr;
                            if (Engine::Memory::TryReadValue(field.valueAddress, target) && target) {
                                state.transformModel.pendingFocusInstance = target;
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Open Transform tab for this object");
                        }
                    }
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

        UiTheme::EndInspectorTable();

        if (!fieldsFilterIsEmpty && visibleFieldCount == 0) {
            ImGui::TextDisabled("No fields match filter.");
        }
    }
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
