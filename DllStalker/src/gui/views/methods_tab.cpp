#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/methods_tab.h"

#include "gui/session_state.h"
#include "gui/config.h"
#include "gui/infra/search_filter.h"
#include "gui/views/dispatch_status.h"
#include "gui/views/invoke_args_modal.h"
#include "dumper/invoke_param_policy.h"
#include "gui/state/runtime/call_log_model.h"
#include "gui/state/navigation/history_steady_time.h"
#include "services/main_thread_dispatcher.h"
#include "types/memory_guard.h"

#include "imgui.h"

#include <array>
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
} // namespace

void RenderMethodsTab(const InspectorCache& inspectorSnapshot,
                      CopyFeedbackState& copyFeedback,
                      bool inspectorLoadInProgress,
                      ControlPanelSessionState& state) {
    if (inspectorSnapshot.methods.empty() && !inspectorLoadInProgress) {
        ImGui::TextUnformatted("No methods available.");
    }

    ImGui::Separator();
    ImGui::BeginChild("MethodsStatusBar", ImVec2(0, 25 * Config::GUI_SCALE), true, ImGuiWindowFlags_NoScrollbar);
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
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Copied: %s", copyFeedback.copiedMethodAddress);
        }
        else if (invokeToastActive) {
            if (s_toast.succeeded) {
                ImGui::TextColored(ImVec4(0.3f, 0.95f, 0.3f, 1.0f), "OK: %s -> %s",
                                   s_toast.methodName.c_str(), s_toast.returnDisplay.c_str());
            }
            else {
                ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "FAIL: %s -> %s",
                                   s_toast.methodName.c_str(),
                                   s_toast.errorMessage.empty() ? "<unknown error>" : s_toast.errorMessage.c_str());
            }
        }
        else {
            RenderDispatchStatus("Method Invoker");
        }
    }
    ImGui::EndChild();

    ImGui::InputText("Filter Methods", state.methodsFilterBuffer, sizeof(state.methodsFilterBuffer));
    if (strcmp(state.methodsCachedOriginalFilter.c_str(), state.methodsFilterBuffer) != 0) {
        state.methodsCachedOriginalFilter = state.methodsFilterBuffer;
        state.methodsCachedLowerFilter = Gui::Infra::SearchFilter::ToLowercase(state.methodsFilterBuffer);
    }
    const bool methodsFilterIsEmpty = state.methodsCachedLowerFilter.empty();

    const std::string sidebarClassName = LookupSidebarClassName(state);

    size_t visibleMethodCount = 0;
    if (ImGui::BeginTable("MethodsTable", 6,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Log",          ImGuiTableColumnFlags_WidthFixed, 32.0f * Config::GUI_SCALE);
        ImGui::TableSetupColumn("Name",         ImGuiTableColumnFlags_WidthStretch, 0.28f);
        ImGui::TableSetupColumn("Return Type",  ImGuiTableColumnFlags_WidthStretch, 0.16f);
        ImGui::TableSetupColumn("Parameters",   ImGuiTableColumnFlags_WidthStretch, 0.16f);
        ImGui::TableSetupColumn("RVA / Offset", ImGuiTableColumnFlags_WidthStretch, 0.18f);
        ImGui::TableSetupColumn("Run",          ImGuiTableColumnFlags_WidthStretch, 0.12f);
        ImGui::TableHeadersRow();

        const bool haveInstance       = inspectorSnapshot.activeInstancePtr != nullptr;
        // Load dispatch flags once per frame (shared by every row).
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
                ImGui::BeginDisabled();
                ImGui::TextDisabled("-");
                ImGui::EndDisabled();
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
                ImGui::PushID("log");
                const char* const logLabel = logging ? "*##log" : "Log##log";
                if (ImGui::Button(logLabel, ImVec2(24.0f * Config::GUI_SCALE, 0))) {
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
                ImGui::PopID();
            }

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(method.name.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(method.returnType.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(method.parameters.c_str());
            ImGui::TableSetColumnIndex(4);

            char addressBuffer[32] = {};
            snprintf(addressBuffer, sizeof(addressBuffer), "0x%llX", static_cast<unsigned long long>(method.address));

            if (ImGui::Selectable(addressBuffer, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    ImGui::SetClipboardText(addressBuffer);
                    strncpy_s(copyFeedback.copiedMethodAddress, sizeof(copyFeedback.copiedMethodAddress), addressBuffer, _TRUNCATE);
                    copyFeedback.copiedAtSeconds = static_cast<float>(ImGui::GetTime());
                }
            }

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

            // Warm accent — Run invokes managed code on the main thread.
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.85f, 0.45f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.55f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.75f, 0.35f, 0.15f, 1.0f));

            ImGui::PushID("run");
            if (!runEnabled) ImGui::BeginDisabled();
            const bool runClicked = ImGui::SmallButton("Run##run");
            if (!runEnabled) ImGui::EndDisabled();
            ImGui::PopStyleColor(3);
            ImGui::PopID();

            if (!runEnabled && disabledReason && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", disabledReason);
            }

            if (runClicked && runEnabled) {
                void* instance = method.isStatic ? nullptr : inspectorSnapshot.activeInstancePtr;
                if (method.paramTypes.empty()) {
                    // 0-arg path: enqueue immediately, no popup.
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

        ImGui::EndTable();

        if (requestOpenInvokePopup) {
            ImGui::OpenPopup("InvokeArgsPopup");
        }

        if (!methodsFilterIsEmpty && visibleMethodCount == 0) {
            ImGui::TextDisabled("No methods match filter.");
        }
    }

    // Invoke-args modal lives at tab scope so its ImGui ID stack matches OpenPopup.
    RenderInvokeArgsPopup(state, inspectorSnapshot);
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
