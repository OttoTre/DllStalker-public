#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/inspector/invoke_args_modal.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"
#include "dumper/invoke/invoke_param_policy.h"

#include "imgui.h"

#include <array>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

namespace Gui::Views
{
namespace
{
constexpr const char* kCustomIntLabel = "(Custom int...)";

std::unordered_set<size_t> s_invokeEnumCustomMode{};

void ClearInvokeEnumCustomMode() {
    s_invokeEnumCustomMode.clear();
}

bool TryParseInvokeBufferInt64(const char* text, int64_t& out) {
    if (!text || text[0] == '\0') {
        return false;
    }
    try {
        out = std::stoll(text, nullptr, 0);
        return true;
    }
    catch (...) {
        return false;
    }
}

void RenderInvokeParamInput(ControlPanelSessionState& state,
                            const Engine::MethodParam& param,
                            size_t paramIndex,
                            std::array<char, 64>& buffer) {
    using Support = Engine::Dumper::InvokeParamSupport;
    const Support support = Engine::Dumper::ClassifyInvokeParam(param);

    char label[64];
    snprintf(label, sizeof(label), "[%zu] %s##invokeArg", paramIndex, param.typeName.c_str());

    ImGui::PushID(static_cast<int>(paramIndex));
    ImGui::SetNextItemWidth(260.0f);

    if (support == Support::Enum) {
        auto& lits = state.enumLiteralCache.byKlass[param.enumKlass];
        if (lits.empty() && state.dumper && param.enumKlass) {
            lits = state.dumper->GetEnumLiterals(param.enumKlass);
        }

        const bool useCustom = lits.empty() || s_invokeEnumCustomMode.count(paramIndex) != 0;
        if (useCustom) {
            ImGui::InputText(label, buffer.data(), buffer.size());
            ImGui::PopID();
            return;
        }

        int64_t currentValue = 0;
        const bool hasParsedValue = TryParseInvokeBufferInt64(buffer.data(), currentValue);

        int matchIndex = -1;
        if (hasParsedValue) {
            for (size_t j = 0; j < lits.size(); ++j) {
                if (lits[j].value == currentValue) {
                    matchIndex = static_cast<int>(j);
                    break;
                }
            }
        }

        if (hasParsedValue && matchIndex < 0) {
            s_invokeEnumCustomMode.insert(paramIndex);
            ImGui::InputText(label, buffer.data(), buffer.size());
            ImGui::PopID();
            return;
        }

        std::vector<const char*> labels;
        labels.reserve(lits.size() + 1);
        for (const auto& lit : lits) {
            labels.push_back(lit.name.c_str());
        }
        labels.push_back(kCustomIntLabel);

        int currentItem = matchIndex >= 0 ? matchIndex : static_cast<int>(lits.size());
        const int previousItem = currentItem;

        if (ImGui::Combo(label, &currentItem, labels.data(), static_cast<int>(labels.size()))) {
            if (currentItem == static_cast<int>(lits.size())) {
                s_invokeEnumCustomMode.insert(paramIndex);
            }
            else if (currentItem >= 0 && currentItem < static_cast<int>(lits.size())
                     && currentItem != previousItem) {
                s_invokeEnumCustomMode.erase(paramIndex);
                snprintf(buffer.data(),
                         buffer.size(),
                         "%lld",
                         static_cast<long long>(lits[static_cast<size_t>(currentItem)].value));
            }
        }
        ImGui::PopID();
        return;
    }

    ImGui::InputText(label, buffer.data(), buffer.size());
    if (support == Support::Reference) {
        ImGui::TextDisabled("null or 0x...");
    }
    ImGui::PopID();
}
} // namespace

void RenderInvokeArgsPopup(ControlPanelSessionState& state, const InspectorCache& inspectorSnapshot) {
    if (!ImGui::BeginPopupModal("InvokeArgsPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    auto& invokeQueue = *state.invokeQueue;
    auto closePopup = [&]() {
        invokeQueue.pendingInvokeMethodIndex = -1;
        invokeQueue.argBuffers.clear();
        ClearInvokeEnumCustomMode();
        ImGui::CloseCurrentPopup();
    };

    // Re-fetch the method by index from the snapshot. The method list can
    // change underneath us if the user picks a different class while the
    // popup is open; bail cleanly in that case.
    const int idx = invokeQueue.pendingInvokeMethodIndex;
    if (idx < 0 || idx >= static_cast<int>(inspectorSnapshot.methods.size())) {
        closePopup();
        ImGui::EndPopup();
        return;
    }

    const auto& method = inspectorSnapshot.methods[idx];
    if (invokeQueue.argBuffers.size() != method.paramTypes.size()) {
        // Same defensive bail as above: list mutated during popup.
        closePopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::Text("Invoke: %s", method.name.c_str());
    ImGui::TextDisabled("Returns: %s", method.returnType.c_str());
    ImGui::Separator();

    for (size_t i = 0; i < method.paramTypes.size(); ++i) {
        RenderInvokeParamInput(state, method.paramTypes[i], i, invokeQueue.argBuffers[i]);
    }

    ImGui::Separator();

    if (UiTheme::PrimaryButton("Run")) {
        std::vector<std::string> args;
        args.reserve(method.paramTypes.size());
        for (const auto& buf : invokeQueue.argBuffers) {
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
