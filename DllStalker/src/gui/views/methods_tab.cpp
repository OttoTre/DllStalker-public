#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/methods_tab.h"

#include "gui/config.h"
#include "gui/infra/search_filter.h"
#include "gui/views/invoke_args_modal.h"
#include "gui/state/call_log_model.h"
#include "gui/state/history_steady_time.h"
#include "services/main_thread_dispatcher.h"
#include "types/type_classifier.h"

#include "imgui.h"

#include <array>
#include <mutex>
#include <string>

namespace Gui::Views
{
namespace
{
// The dumper-side InvokeMethod marshalling supports numeric primitives,
// bool, and System.String. Anything else (managed reference types,
// arrays, lists, value-type structs, generics) gets the Run button
// disabled in the UI so the user doesn't trigger a deferred error
// toast for something we already know we can't handle.
bool MethodArgsAreInvokable(const Engine::MethodInfo& method) {
    using Cat = Engine::Types::TypeCategory;
    for (const auto& p : method.paramTypes) {
        const Cat cat = Engine::Types::GetCategory(p.typeName);
        switch (cat) {
        case Cat::I1: case Cat::I2: case Cat::I4: case Cat::I8:
        case Cat::U1: case Cat::U2: case Cat::U4: case Cat::U8:
        case Cat::R4: case Cat::R8: case Cat::BOOLEAN: case Cat::STRING:
            continue;
        default:
            return false;
        }
    }
    return true;
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
} // namespace

void RenderMethodsTab(const InspectorCache& inspectorSnapshot,
                      AppShell::CopyFeedbackState& copyFeedback,
                      bool inspectorLoadInProgress,
                      ControlPanelSessionState& state) {
    if (inspectorSnapshot.methods.empty() && !inspectorLoadInProgress) {
        ImGui::TextUnformatted("No methods available.");
    }

    ImGui::Separator();
    ImGui::BeginChild("MethodsStatusBar", ImVec2(0, 25 * Config::GUI_SCALE), true, ImGuiWindowFlags_NoScrollbar);
    {
        // Method Invoker toast. The dispatcher worker bumps
        // latestInvokeResultVersion under invokeResultMutex; we cache the
        // last-observed version on this (GUI) thread and only acquire the
        // mutex when it changes. That means the steady-state per-frame cost
        // is one atomic load and a couple of branches -- no contention with
        // an in-flight invoke that's holding the mutex while running on the
        // main thread.
        struct InvokeToastCache {
            int                  version             = 0;
            float                stampedAtSeconds    = -1.0f;
            bool                 succeeded           = false;
            std::string          methodName{};
            std::string          returnDisplay{};
            std::string          errorMessage{};
        };
        static InvokeToastCache s_toast;

        const int currentInvokeVersion = state.latestInvokeResultVersion.load(std::memory_order_acquire);
        if (currentInvokeVersion != s_toast.version) {
            // New result available. Take the mutex once to copy the snapshot
            // out, stamp wall-clock time on the GUI thread, then drop the
            // lock. Subsequent frames render from the cache lock-free.
            std::lock_guard<std::mutex> lock(state.invokeResultMutex);
            s_toast.version          = currentInvokeVersion;
            s_toast.stampedAtSeconds = static_cast<float>(ImGui::GetTime());
            s_toast.succeeded        = state.latestInvokeResult.succeeded;
            s_toast.methodName       = state.latestInvokeMethodName;
            s_toast.returnDisplay    = state.latestInvokeResult.returnDisplay;
            s_toast.errorMessage     = state.latestInvokeResult.error;
            // Mirror the wall-clock stamp back into the session state so
            // any other reader can observe it; harmless redundancy with
            // the cache, but lets future code (e.g. logging) consult one
            // place. GUI thread is the only writer of this field.
            state.latestInvokeResultAtSeconds = s_toast.stampedAtSeconds;

            State::MethodAuditPayload audit{};
            audit.methodName    = s_toast.methodName;
            audit.parameters    = state.latestInvokeMethodParameters;
            audit.argsDisplay   = state.latestInvokeArgsDisplay;
            audit.succeeded     = s_toast.succeeded;
            audit.returnDisplay = s_toast.returnDisplay;
            audit.error         = s_toast.errorMessage;
            state.RecordMethodAudit(audit);
        }

        const float now = static_cast<float>(ImGui::GetTime());
        const bool  copyToastActive   = copyFeedback.copiedAtSeconds > 0 && (now - copyFeedback.copiedAtSeconds) < 2.0f;
        const bool  invokeToastActive = s_toast.stampedAtSeconds > 0 && (now - s_toast.stampedAtSeconds) < 3.0f;

        const bool dispatchAvailable = Engine::Services::MainThreadDispatcher::IsDispatchAvailable();
        const bool mainThreadCaptured = Engine::Services::MainThreadDispatcher::IsMainThreadCaptured();

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
        else if (!dispatchAvailable) {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                               "Method Invoker disabled (runtime_invoke hook unavailable)");
        }
        else if (!mainThreadCaptured) {
            ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.25f, 1.0f),
                               "Waiting for engine to make a managed call (main thread not yet captured)...");
        }
        else {
            ImGui::TextDisabled("Tip: Double-click RVA to copy. Use Run to invoke (main thread = 0x%lX).",
                                static_cast<unsigned long>(Engine::Services::MainThreadDispatcher::GetMainThreadId()));
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
        // Cache once per frame; reading these atomics is cheap but we hit
        // them per-row, so a single load up here keeps the loop tight and
        // makes "why is this row disabled?" deterministic across rows.
        const bool dispatchReady      = Engine::Services::MainThreadDispatcher::IsDispatchAvailable()
                                     && Engine::Services::MainThreadDispatcher::IsMainThreadCaptured();
        const bool dispatchUnavailable = !Engine::Services::MainThreadDispatcher::IsDispatchAvailable();

        // Deferred popup open: ImGui hashes the popup ID against the
        // current ID stack at OpenPopup time, so we cannot call OpenPopup
        // from inside the per-row PushID/PopID block (BeginPopupModal at
        // the outer scope would never see the matching ID). Capture the
        // request and dispatch it after EndTable instead.
        bool requestOpenInvokePopup = false;

        for (size_t methodIndex = 0; methodIndex < inspectorSnapshot.methods.size(); ++methodIndex) {
            const auto& method = inspectorSnapshot.methods[methodIndex];
            if (!methodsFilterIsEmpty && !Gui::Infra::SearchFilter::MethodMatches(method, state.methodsCachedLowerFilter)) {
                continue;
            }
            ++visibleMethodCount;
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(methodIndex));

            ImGui::TableSetColumnIndex(0);
            const bool logEligible = State::CallLogModel::IsLogEligible(method);
            if (!logEligible) {
                ImGui::BeginDisabled();
                ImGui::TextDisabled("-");
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (method.address == 0) {
                        ImGui::SetTooltip("Native address unavailable");
                    }
                    else if (!method.paramsKnown && !method.paramTypes.empty()) {
                        ImGui::SetTooltip("Param signature unknown");
                    }
                    else if (!MethodArgsAreInvokable(method)) {
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

            // Decide whether the Run button is clickable. Reasons it might
            // not be (each gets its own tooltip so the user knows why):
            //   * Method Invoker globally disabled (hook didn't install)
            //   * Main thread not yet captured (game still loading)
            //   * Param signature unknown (Mono build missing exports)
            //   * Args contain types we can't marshal in v1
            //   * Instance method without an active instance selected
            //   * Engine handle missing (shouldn't happen, but defensive)
            const bool argsKnown   = method.paramsKnown || method.paramTypes.empty();
            const bool argsOk      = MethodArgsAreInvokable(method);
            const bool needsInst   = !method.isStatic;
            const bool handleOk    = method.engineHandle != nullptr;
            const bool runEnabled  = dispatchReady
                                  && argsKnown && argsOk && handleOk
                                  && (!needsInst || haveInstance);

            const char* disabledReason = nullptr;
            if (dispatchUnavailable)        disabledReason = "Method Invoker disabled (hook unavailable)";
            else if (!dispatchReady)        disabledReason = "Main thread not yet captured";
            else if (!handleOk)             disabledReason = "Method handle missing";
            else if (!argsKnown)            disabledReason = "Param signature unknown";
            else if (!argsOk)               disabledReason = "Arg type not supported in v1";
            else if (needsInst && !haveInstance) disabledReason = "No active instance";

            // Orange/red palette per Feature-Plan-2: signal "this is an
            // unsafe action".
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
                    state.pendingInvokeMethodIndex = static_cast<int>(methodIndex);
                    state.invokeArgBuffers.assign(method.paramTypes.size(), std::array<char, 64>{});
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

    // Popup is a child of the Methods tab so it inherits the tab's
    // ID stack (avoids cross-tab modal collisions).
    RenderInvokeArgsPopup(state, inspectorSnapshot);
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
