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
//
// The GUI thread wraps the click into a Job, hands it to
// MainThreadDispatcher. The runtime_invoke detour drains the job on the
// engine's main thread (the first non-DllStalker thread to reach the
// detour latches as the main thread). The Job captures the dumper pointer
// + method copy + instance + parsed args by value, so it stays self-
// contained even after the GUI rebuilds its method list.
//
// Failure modes that surface as immediate synthetic results (no enqueue):
//   * Dumper not initialized
//   * runtime_invoke hook didn't install (very rare; usually means the
//     export wasn't resolved or MinHook conflicted with another tool)
//   * Main thread not yet captured (game hasn't reached the title scene
//     yet -- in practice resolves within milliseconds of process start)
//
// We deliberately do NOT fall back to a synchronous GUI-thread invoke
// when the hook is unavailable. The previous design did that with a
// warning, but it crashes hard on any method that touches the scene
// graph and there's no way to know which methods are safe ahead of time.
// Better to disable Run cleanly and let the user know.
void ControlPanelSessionState::EnqueueInvoke(const Engine::MethodInfo& method,
                                              void* instance,
                                              std::vector<std::string> args) {
    auto failImmediately = [&](const char* reason) {
        std::lock_guard<std::mutex> lock(invokeResultMutex);
        latestInvokeResult.succeeded     = false;
        latestInvokeResult.error         = reason;
        latestInvokeResult.returnDisplay = "<error>";
        latestInvokeMethodName           = method.name;
        latestInvokeResultAtSeconds      = -1.0f;
        latestInvokeResultVersion.fetch_add(1);
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

    auto runOne = [this, dumperRef, methodCopy, instance, argsCopy,
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

        std::lock_guard<std::mutex> lock(invokeResultMutex);
        latestInvokeResult           = std::move(result);
        latestInvokeMethodName       = methodCopy.name;
        latestInvokeMethodParameters = methodParametersCopy;
        latestInvokeArgsDisplay      = argsDisplayCopy;
        // -1.0f sentinel: the GUI thread stamps the wall-clock time
        // when it next reads the new version, so the toast is timed
        // relative to when the user sees it (not when it ran).
        latestInvokeResultAtSeconds  = -1.0f;
        latestInvokeResultVersion.fetch_add(1);
    };

    Engine::Services::MainThreadDispatcher::Enqueue(std::move(runOne));
}
} // namespace Gui

#endif // ENABLE_DUMPER
