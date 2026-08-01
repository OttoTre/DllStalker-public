#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/methods_tab.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"
#include "gui/config.h"
#include "gui/infra/search_filter.h"
#include "gui/views/class_label_lookup.h"
#include "gui/views/dispatch_status.h"
#include "gui/views/invoke_args_modal.h"
#include "dumper/invoke_param_policy.h"
#include "gui/state/runtime/call_log_model.h"
#include "gui/state/navigation/history_steady_time.h"
#include "services/main_thread_dispatcher.h"
#include "types/memory_guard.h"

#include "imgui.h"

#include <array>
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>

namespace Gui::Views
{
namespace
{
std::string LookupSidebarClassName(const ControlPanelSessionState& state) {
    if (!state.selectedClass) {
        return {};
    }
    const std::string label = LookupClassDisplayName(state, state.selectedClass);
    return label.empty() ? std::string("<class>") : label;
}
} // namespace

void RenderMethodsTab(const InspectorCache& inspectorSnapshot,
                      CopyFeedbackState& copyFeedback,
                      bool inspectorLoadInProgress,
                      ControlPanelSessionState& state) {
    if (inspectorSnapshot.methods.empty() && !inspectorLoadInProgress) {
        ImGui::TextUnformatted("No methods available.");
    }

    ImGui::Separator();
    {
        // Invoke toast: read latestVersion; lock invokeQueue only when it changes.
        struct InvokeToastCache {
            int                  version             = 0;
            float                stampedAtSeconds    = -1.0f;
            bool                 succeeded           = false;
            std::string          methodName{};
            std::string          returnDisplay{};
            std::string          errorMessage{};
        };
        static InvokeToastCache s_toast;

        const int currentInvokeVersion = state.invokeQueue.latestVersion.load(std::memory_order_acquire);
        if (currentInvokeVersion != s_toast.version) {
            // Copy the latest result into thread-local toast state.
            std::lock_guard<std::mutex> lock(state.invokeQueue.mutex);
            s_toast.version          = currentInvokeVersion;
            s_toast.stampedAtSeconds = static_cast<float>(ImGui::GetTime());
            s_toast.succeeded        = state.invokeQueue.latestResult.succeeded;
            s_toast.methodName       = state.invokeQueue.latestMethodName;
            s_toast.returnDisplay    = state.invokeQueue.latestResult.returnDisplay;
            s_toast.errorMessage     = state.invokeQueue.latestResult.error;
            state.invokeQueue.latestAtSeconds = s_toast.stampedAtSeconds;

            State::MethodAuditPayload audit{};
            audit.methodName    = s_toast.methodName;
            audit.parameters    = state.invokeQueue.latestMethodParameters;
            audit.argsDisplay   = state.invokeQueue.latestArgsDisplay;
            audit.succeeded     = s_toast.succeeded;
            audit.returnDisplay = s_toast.returnDisplay;
            audit.error         = s_toast.errorMessage;
            state.RecordMethodAudit(audit);
        }

        const float now = static_cast<float>(ImGui::GetTime());
        const bool  copyToastActive   = copyFeedback.copiedAtSeconds > 0 && (now - copyFeedback.copiedAtSeconds) < 2.0f;
        const bool  invokeToastActive = s_toast.stampedAtSeconds > 0 && (now - s_toast.stampedAtSeconds) < 3.0f;

        if (copyToastActive) {
            char msg[96] = {};
            std::snprintf(msg, sizeof(msg), "Copied: %s", copyFeedback.copiedMethodAddress);
            UiTheme::DrawSuccessText(msg);
        }
        else if (invokeToastActive) {
            char msg[384] = {};
            if (s_toast.succeeded) {
                std::snprintf(msg, sizeof(msg), "OK: %s -> %s",
                              s_toast.methodName.c_str(), s_toast.returnDisplay.c_str());
                UiTheme::DrawSuccessText(msg);
            }
            else {
                std::snprintf(msg, sizeof(msg), "FAIL: %s -> %s",
                              s_toast.methodName.c_str(),
                              s_toast.errorMessage.empty() ? "<unknown error>" : s_toast.errorMessage.c_str());
                UiTheme::DrawErrorText(msg);
            }
        }
        else {
            RenderDispatchStatus("Method Invoker");
        }
    }

    UiTheme::ElevatedFilter("##methods_filter", state.methodsFilterBuffer,
                            sizeof(state.methodsFilterBuffer), -1.0f, "Filter...");
    if (strcmp(state.methodsCachedOriginalFilter.c_str(), state.methodsFilterBuffer) != 0) {
        state.methodsCachedOriginalFilter = state.methodsFilterBuffer;
        state.methodsCachedLowerFilter = Gui::Infra::SearchFilter::ToLowercase(state.methodsFilterBuffer);
    }
    const bool methodsFilterIsEmpty = state.methodsCachedLowerFilter.empty();

    const std::string sidebarClassName = LookupSidebarClassName(state);

    size_t visibleMethodCount = 0;
    const bool skipEmptyLoadingTable =
        inspectorLoadInProgress && inspectorSnapshot.methods.empty();
    if (skipEmptyLoadingTable) {
        ImGui::TextDisabled("Waiting for methods...");
    }
    else if (UiTheme::BeginInspectorTable("MethodsTable", 6)) {
        const float logHeaderW = ImGui::CalcTextSize("Log").x + ImGui::GetStyle().CellPadding.x * 2.0f;
        const float logColW = (std::max)(28.0f * Config::GUI_SCALE, logHeaderW);
        ImGui::TableSetupColumn("Log",          ImGuiTableColumnFlags_WidthFixed, logColW);
        ImGui::TableSetupColumn("Name",         ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupColumn("Return Type",  ImGuiTableColumnFlags_WidthStretch, 0.16f);
        ImGui::TableSetupColumn("Parameters",   ImGuiTableColumnFlags_WidthStretch, 0.16f);
        ImGui::TableSetupColumn("RVA / Offset", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableSetupColumn("Run",          ImGuiTableColumnFlags_WidthStretch, 0.12f);
        ImGui::TableHeadersRow();

        const bool haveInstance       = inspectorSnapshot.activeInstancePtr != nullptr;
        const bool dispatchReady      = Engine::Services::MainThreadDispatcher::IsDispatchAvailable()
                                     && Engine::Services::MainThreadDispatcher::IsMainThreadCaptured();
        const bool dispatchUnavailable = !Engine::Services::MainThreadDispatcher::IsDispatchAvailable();

        // OpenPopup must run outside the per-row PushID scope — defer until after EndTable.
        bool requestOpenInvokePopup = false;

        for (size_t methodIndex = 0; methodIndex < inspectorSnapshot.methods.size(); ++methodIndex) {
            const auto& method = inspectorSnapshot.methods[methodIndex];
            if (!methodsFilterIsEmpty && !Gui::Infra::SearchFilter::MethodMatches(method, state.methodsCachedLowerFilter)) {
                continue;
            }
            ++visibleMethodCount;
            ImGui::TableNextRow();
            if (method.jitFailed) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(85, 30, 30, 60));
            }
            const bool execOk = method.address == 0
                || Engine::Memory::IsExecutablePointer(reinterpret_cast<const void*>(method.address));
            ImGui::PushID(static_cast<int>(methodIndex));

            ImGui::TableSetColumnIndex(0);
            const bool logEligible = State::CallLogModel::IsLogEligible(method);
            if (!logEligible) {
                UiTheme::CenteredDisabledGlyph("-", logColW);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (method.jitFailed) {
                        ImGui::SetTooltip("JIT compile failed (open generic / unsupported)");
                    }
                    else if (method.address == 0) {
                        ImGui::SetTooltip("Native address unavailable");
                    }
                    else if (!execOk) {
                        ImGui::SetTooltip("Method address not executable");
                    }
                    else if (!method.paramsKnown && !method.paramTypes.empty()) {
                        ImGui::SetTooltip("Param signature unknown");
                    }
                    else if (!Engine::Dumper::MethodIsInvokable(method)) {
                        ImGui::SetTooltip("Arg type not supported for call logging");
                    }
                    else {
                        ImGui::SetTooltip("Too many register arguments (max 3 instance / 4 static)");
                    }
                }
            }
            else {
                const bool logging = state.callLog.IsLogging(method.address);
                const char* const logLabel = logging ? "*" : "L";
                const ImVec4* logColor = logging ? &UiTheme::Tokens().error : nullptr;
                if (UiTheme::CenteredGlyphButton("##log", logLabel, logColW, logColor)) {
                    const auto result = state.callLog.Toggle(method, sidebarClassName);
                    switch (result) {
                    case State::CallLogModel::ToggleResult::RejectedCap:
                        state.navigationFeedback.MarkStatus("Call log hook cap (16)",
                                                            State::HistorySteadyNowSeconds());
                        break;
                    case State::CallLogModel::ToggleResult::RejectedAlreadyHooked:
                        state.navigationFeedback.MarkStatus("Method address already hooked",
                                                            State::HistorySteadyNowSeconds());
                        break;
                    case State::CallLogModel::ToggleResult::RejectedInstallFailed:
                        state.navigationFeedback.MarkStatus("Failed to install call log hook",
                                                            State::HistorySteadyNowSeconds());
                        break;
                    default:
                        break;
                    }
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip(logging ? "Stop logging calls" : "Log native calls to Logger dock");
                }
            }

            ImGui::TableSetColumnIndex(1);
            UiTheme::DrawColumnName(method.name.c_str());
            ImGui::TableSetColumnIndex(2);
            UiTheme::DrawColumnType(method.returnType.c_str());
            ImGui::TableSetColumnIndex(3);
            UiTheme::DrawColumnType(method.parameters.c_str());
            ImGui::TableSetColumnIndex(4);

            char addressBuffer[32] = {};
            snprintf(addressBuffer, sizeof(addressBuffer), "0x%llX", static_cast<unsigned long long>(method.address));

            ImGui::PushStyleColor(ImGuiCol_Text, UiTheme::Tokens().semantic_link);
            ImGui::AlignTextToFramePadding();
            if (ImGui::Selectable(addressBuffer, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    ImGui::SetClipboardText(addressBuffer);
                    strncpy_s(copyFeedback.copiedMethodAddress, sizeof(copyFeedback.copiedMethodAddress), addressBuffer, _TRUNCATE);
                    copyFeedback.copiedAtSeconds = static_cast<float>(ImGui::GetTime());
                }
            }
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(5);

            // Run gating — disabledReason tooltips explain each failure mode.
            const bool argsKnown   = method.paramsKnown || method.paramTypes.empty();
            const bool argsOk      = Engine::Dumper::MethodIsInvokable(method);
            const bool needsInst   = !method.isStatic;
            const bool handleOk    = method.engineHandle != nullptr;
            const bool runEnabled  = dispatchReady
                                  && argsKnown && argsOk && handleOk
                                  && execOk
                                  && !method.jitFailed
                                  && (!needsInst || haveInstance);

            const char* disabledReason = nullptr;
            if (method.jitFailed)           disabledReason = "JIT compile failed (open generic / unsupported)";
            else if (dispatchUnavailable)   disabledReason = "Method Invoker disabled (hook unavailable)";
            else if (!dispatchReady)        disabledReason = "Main thread not yet captured";
            else if (!handleOk)             disabledReason = "Method handle missing";
            else if (!execOk)               disabledReason = "Method address not executable";
            else if (!argsKnown)            disabledReason = "Param signature unknown";
            else if (!argsOk)               disabledReason = "Arg type not supported";
            else if (needsInst && !haveInstance) disabledReason = "No active instance";

            ImGui::PushID("run");
            if (!runEnabled) ImGui::BeginDisabled();
            const bool runClicked = UiTheme::IconPlayButton("##run", "Invoke");
            if (!runEnabled) ImGui::EndDisabled();
            ImGui::PopID();

            if (!runEnabled && disabledReason && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", disabledReason);
            }

            if (runClicked && runEnabled) {
                void* instance = method.isStatic ? nullptr : inspectorSnapshot.activeInstancePtr;
                if (method.paramTypes.empty()) {
                    state.EnqueueInvoke(method, instance, {});
                }
                else {
                    // Stage the open request; the actual OpenPopup call
                    // happens after EndTable so the popup ID is hashed
                    // against the same ID stack BeginPopupModal sees.
                    state.invokeQueue.pendingInvokeMethodIndex = static_cast<int>(methodIndex);
                    state.invokeQueue.argBuffers.assign(method.paramTypes.size(), std::array<char, 64>{});
                    requestOpenInvokePopup = true;
                }
            }

            ImGui::PopID();
        }

        UiTheme::EndInspectorTable();

        if (requestOpenInvokePopup) {
            ImGui::OpenPopup("InvokeArgsPopup");
        }

        if (!methodsFilterIsEmpty && visibleMethodCount == 0) {
            ImGui::TextDisabled("No methods match filter.");
        }
    }

    RenderInvokeArgsPopup(state, inspectorSnapshot);
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
