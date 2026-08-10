#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/session_state.h"

#include "services/main_thread_dispatcher.h"

namespace Gui
{
namespace
{
std::string FormatInvokeArgsDisplay(const std::vector<std::string>& args) {
    if (args.empty()) {
        return "()";
    }
    std::string out = "(";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += args[i];
    }
    out += ")";
    return out;
}
} // namespace

// ==== Method Invoker =================================================
// GUI → MainThreadDispatcher job; runtime_invoke drains on Unity main.
// Result sink is shared_ptr<InvokeRequestQueue> (panel-close safe).
// No sync GUI-thread fallback when the hook/main is unavailable.
void ControlPanelSessionState::EnqueueInvoke(const Engine::MethodInfo& method,
                                              void* instance,
                                              std::vector<std::string> args) {
    auto queue = invokeQueue;
    if (!queue) {
        return;
    }

    auto failImmediately = [&](const char* reason) {
        std::lock_guard<std::mutex> lock(queue->mutex);
        queue->latestResult.succeeded     = false;
        queue->latestResult.error         = reason;
        queue->latestResult.returnDisplay = "<error>";
        queue->latestMethodName           = method.name;
        queue->latestMethodParameters     = method.parameters;
        queue->latestArgsDisplay          = FormatInvokeArgsDisplay(args);
        queue->latestAtSeconds            = -1.0f;
        queue->pendingMethodAudit         = true;
        queue->latestVersion.fetch_add(1);
    };

    if (!dumper) {
        failImmediately("Dumper not initialized");
        return;
    }
    if (!Engine::Services::MainThreadDispatcher::IsDispatchAvailable()) {
        failImmediately("Method Invoker disabled (runtime_invoke hook unavailable)");
        return;
    }
    if (!Engine::Services::MainThreadDispatcher::IsMainThreadCaptured()) {
        failImmediately("Main thread not yet captured (wait for the game to load)");
        return;
    }

    auto dumperRef            = dumper;
    auto methodCopy           = method;
    auto argsCopy             = std::move(args);
    auto methodParametersCopy = method.parameters;
    auto argsDisplayCopy      = FormatInvokeArgsDisplay(argsCopy);

    auto runOne = [queue, dumperRef, methodCopy, instance, argsCopy,
                   methodParametersCopy, argsDisplayCopy]() {
        Engine::InvokeResult result;
        try {
            result = dumperRef->InvokeMethod(methodCopy, instance, argsCopy);
        }
        catch (const std::exception& e) {
            result.succeeded     = false;
            result.error         = std::string("Host exception: ") + e.what();
            result.returnDisplay = "<error>";
        }
        catch (...) {
            result.succeeded     = false;
            result.error         = "Host exception (unknown)";
            result.returnDisplay = "<error>";
        }

        std::lock_guard<std::mutex> lock(queue->mutex);
        queue->latestResult           = std::move(result);
        queue->latestMethodName       = methodCopy.name;
        queue->latestMethodParameters = methodParametersCopy;
        queue->latestArgsDisplay      = argsDisplayCopy;
        // -1.0f sentinel: the GUI thread stamps the wall-clock time
        // when it next reads the new version, so the toast is timed
        // relative to when the user sees it (not when it ran).
        queue->latestAtSeconds      = -1.0f;
        queue->pendingMethodAudit   = true;
        queue->latestVersion.fetch_add(1);
    };

    Engine::Services::MainThreadDispatcher::Enqueue(std::move(runOne));
}
} // namespace Gui

#endif // ENABLE_DUMPER
