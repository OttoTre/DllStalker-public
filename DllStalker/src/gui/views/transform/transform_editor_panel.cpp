#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/transform/transform_editor_panel.h"

#include "gui/config.h"
#include "gui/session_state.h"
#include "gui/state/transform/transform_model.h"

#include "imgui.h"

#include <cstdio>
#include <mutex>
#include <string>

namespace Gui::Views
{
namespace
{
bool Vec3Dirty(const float* values, const Gui::State::Vec3f& baseline) {
    return values[0] != baseline.x || values[1] != baseline.y || values[2] != baseline.z;
}

// Renders XYZ inputs only. Returns true if Enter was pressed in any axis field.
bool RenderVec3Inputs(const char* label,
                      float* values,
                      const Gui::State::Vec3f& baseline,
                      bool disabled) {
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SameLine();

    constexpr ImVec4 kDirtyBg{ 0.55f, 0.42f, 0.12f, 1.0f };
    const float      baselineComps[3] = { baseline.x, baseline.y, baseline.z };
    const char*      axisLabels[3]    = { "X##v", "Y##v", "Z##v" };

    bool enterPressed = false;
    if (disabled) {
        ImGui::BeginDisabled();
    }

    for (int axis = 0; axis < 3; ++axis) {
        if (axis > 0) {
            ImGui::SameLine();
        }
        ImGui::SetNextItemWidth(70.0f * Gui::Config::GUI_SCALE);
        const bool dirty = values[axis] != baselineComps[axis];
        if (dirty) {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, kDirtyBg);
        }
        ImGui::InputFloat(axisLabels[axis], &values[axis], 0.f, 0.f, "%.3f");
        if (ImGui::IsItemDeactivatedAfterEdit()
            && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
            enterPressed = true;
        }
        if (dirty) {
            ImGui::PopStyleColor();
        }
    }

    if (disabled) {
        ImGui::EndDisabled();
    }
    ImGui::PopID();
    return enterPressed;
}

struct TransformDisplaySnapshot {
    std::string name{};
    Gui::State::Vec3f localPosition{};
    Gui::State::Vec3f localEuler{};
    Gui::State::Vec3f localScale{};
    Gui::State::Vec3f worldPosition{};
    void*       parentPtr     = nullptr;
    void*       parentKlass   = nullptr;
    std::string parentName{};
    bool        activeSelf      = false;
    bool        hasActiveSelf   = false;
};

TransformDisplaySnapshot CopyDisplaySnapshot(const Gui::State::TransformModel& model) {
    TransformDisplaySnapshot snap{};
    std::lock_guard<std::mutex> lock(model.cacheMutex);
    snap.name           = model.name;
    snap.localPosition  = model.localPosition;
    snap.localEuler     = model.localEuler;
    snap.localScale     = model.localScale;
    snap.worldPosition  = model.worldPosition;
    snap.parentPtr      = model.parentPtr;
    snap.parentKlass    = model.parentKlass;
    snap.parentName     = model.parentName;
    snap.activeSelf     = model.activeSelf;
    snap.hasActiveSelf  = model.hasActiveSelf;
    return snap;
}

void MaybeSeedEditBuffers(Gui::State::TransformModel& model,
                          const TransformDisplaySnapshot& snap,
                          bool userDraft) {
    if (!model.needsSeed || userDraft || ImGui::IsAnyItemActive()) {
        return;
    }
    model.editPos[0]   = snap.localPosition.x;
    model.editPos[1]   = snap.localPosition.y;
    model.editPos[2]   = snap.localPosition.z;
    model.editRot[0]   = snap.localEuler.x;
    model.editRot[1]   = snap.localEuler.y;
    model.editRot[2]   = snap.localEuler.z;
    model.editScale[0] = snap.localScale.x;
    model.editScale[1] = snap.localScale.y;
    model.editScale[2] = snap.localScale.z;
    model.needsSeed    = false;
    model.editSyncedCacheGeneration =
        model.cacheGeneration.load(std::memory_order_relaxed);
}
} // namespace

void RenderTransformEditorPanel(ControlPanelSessionState& state,
                                Gui::State::TransformModel& model,
                                const Gui::State::TransformSource& selected) {
    const bool fetchedNow = model.fetched.load();
    if (fetchedNow && !model.hadFetchedLastFrame) {
        model.needsSeed = true;
    }
    model.hadFetchedLastFrame = fetchedNow;

    const TransformDisplaySnapshot display = CopyDisplaySnapshot(model);
    const uint32_t cacheGen = model.cacheGeneration.load(std::memory_order_relaxed);

    const bool posDirtyPre   = Vec3Dirty(model.editPos, display.localPosition);
    const bool rotDirtyPre   = Vec3Dirty(model.editRot, display.localEuler);
    const bool scaleDirtyPre = Vec3Dirty(model.editScale, display.localScale);
    const bool anyDirtyPre   = posDirtyPre || rotDirtyPre || scaleDirtyPre;
    const bool userDraftPre  = anyDirtyPre && (model.editSyncedCacheGeneration == cacheGen);

    if (cacheGen != model.lastSeenCacheGeneration && !ImGui::IsAnyItemActive() && !userDraftPre) {
        model.needsSeed = true;
    }

    MaybeSeedEditBuffers(model, display, userDraftPre);
    model.lastSeenCacheGeneration = cacheGen;

    ImGui::Text("Selected: %s (%s)", selected.label.c_str(), selected.typeName.c_str());
    ImGui::SameLine();
    char selPtr[32]{};
    snprintf(selPtr, sizeof(selPtr), "0x%llX",
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(selected.instancePtr)));
    ImGui::TextDisabled("%s", selPtr);
    if (!display.name.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("name \"%s\"", display.name.c_str());
    }

    if (ImGui::SmallButton("Refresh##transformTab")) {
        model.InvalidateFetch();
        model.EnqueueFetch(state, model.selectedIndex);
    }

    const bool disabled = model.editLocked;

    bool enterPressed = false;
    enterPressed |= RenderVec3Inputs("Local Position", model.editPos, display.localPosition, disabled);
    enterPressed |= RenderVec3Inputs("Local Rotation", model.editRot, display.localEuler, disabled);
    enterPressed |= RenderVec3Inputs("Local Scale", model.editScale, display.localScale, disabled);

    const bool posDirty   = Vec3Dirty(model.editPos, display.localPosition);
    const bool rotDirty   = Vec3Dirty(model.editRot, display.localEuler);
    const bool scaleDirty = Vec3Dirty(model.editScale, display.localScale);
    const bool anyDirty   = posDirty || rotDirty || scaleDirty;
    model.editsDirty      = anyDirty && (model.editSyncedCacheGeneration == cacheGen);

    if (anyDirty) {
        ImGui::TextDisabled("(unapplied changes)");
    }

    if (disabled) {
        ImGui::BeginDisabled();
    }
    const bool applyClicked  = ImGui::Button("Apply");
    ImGui::SameLine();
    const bool revertClicked = ImGui::Button("Revert");
    if (disabled) {
        ImGui::EndDisabled();
    }

    if (revertClicked) {
        model.editPos[0]   = display.localPosition.x;
        model.editPos[1]   = display.localPosition.y;
        model.editPos[2]   = display.localPosition.z;
        model.editRot[0]   = display.localEuler.x;
        model.editRot[1]   = display.localEuler.y;
        model.editRot[2]   = display.localEuler.z;
        model.editScale[0] = display.localScale.x;
        model.editScale[1] = display.localScale.y;
        model.editScale[2] = display.localScale.z;
    }

    if (!disabled && (applyClicked || enterPressed)) {
        const int idx = model.selectedIndex;
        if (posDirty) {
            model.EnqueueApplyLocalPosition(state, idx,
                                            { model.editPos[0], model.editPos[1], model.editPos[2] });
        }
        if (rotDirty) {
            model.EnqueueApplyLocalEuler(state, idx,
                                         { model.editRot[0], model.editRot[1], model.editRot[2] });
        }
        if (scaleDirty) {
            model.EnqueueApplyLocalScale(state, idx,
                                         { model.editScale[0], model.editScale[1], model.editScale[2] });
        }
    }

    const auto& world = display.worldPosition;
    ImGui::Text("World Position  (%.3f, %.3f, %.3f)", world.x, world.y, world.z);

    if (!display.parentName.empty() || display.parentPtr != nullptr) {
        ImGui::Text("Parent: %s", display.parentName.empty() ? "?" : display.parentName.c_str());
    }
    else {
        ImGui::TextDisabled("Parent: (none)");
    }

    if (display.hasActiveSelf || selected.isGameObject
        || selected.kind == Gui::State::TransformSourceKind::ImplicitGameObject) {
        bool active = display.activeSelf;
        if (disabled) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Checkbox("Active (activeSelf)", &active)) {
            if (!disabled) {
                model.EnqueueApplyActive(state, model.selectedIndex, active);
            }
        }
        if (disabled) {
            ImGui::EndDisabled();
        }
    }
}

} // namespace Gui::Views

#endif // ENABLE_DUMPER
