#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/invoke_args_modal.h"

#include "imgui.h"

#include <string>
#include <vector>

namespace Gui::Views
{
void RenderInvokeArgsPopup(ControlPanelSessionState& state, const InspectorCache& inspectorSnapshot) {
    if (!ImGui::BeginPopupModal("InvokeArgsPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    auto closePopup = [&]() {
        state.pendingInvokeMethodIndex = -1;
        state.invokeArgBuffers.clear();
        ImGui::CloseCurrentPopup();
    };

    // Re-fetch the method by index from the snapshot. The method list can
    // change underneath us if the user picks a different class while the
    // popup is open; bail cleanly in that case.
    const int idx = state.pendingInvokeMethodIndex;
    if (idx < 0 || idx >= static_cast<int>(inspectorSnapshot.methods.size())) {
        closePopup();
        ImGui::EndPopup();
        return;
    }

    const auto& method = inspectorSnapshot.methods[idx];
    if (state.invokeArgBuffers.size() != method.paramTypes.size()) {
        // Same defensive bail as above: list mutated during popup.
        closePopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::Text("Invoke: %s", method.name.c_str());
    ImGui::TextDisabled("Returns: %s", method.returnType.c_str());
    ImGui::Separator();

    for (size_t i = 0; i < method.paramTypes.size(); ++i) {
        char label[64];
        snprintf(label, sizeof(label), "[%zu] %s##invokeArg", i, method.paramTypes[i].typeName.c_str());
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText(label, state.invokeArgBuffers[i].data(), state.invokeArgBuffers[i].size());
    }

    ImGui::Separator();

    if (ImGui::Button("Run", ImVec2(120, 0))) {
        std::vector<std::string> args;
        args.reserve(method.paramTypes.size());
        for (const auto& buf : state.invokeArgBuffers) {
            args.emplace_back(buf.data());
        }
        void* instance = method.isStatic ? nullptr : inspectorSnapshot.activeInstancePtr;
        state.EnqueueInvoke(method, instance, std::move(args));
        closePopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        closePopup();
    }

    ImGui::EndPopup();
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
