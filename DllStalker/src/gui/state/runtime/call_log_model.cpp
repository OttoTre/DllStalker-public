#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/runtime/call_log_model.h"

#include "services/call_log_hooks.h"

#include <algorithm>

#include "dumper/invoke_param_policy.h"
#include "types/memory_guard.h"
#include "types/value_decoder.h"

#include <cstdio>

namespace Gui::State
{
namespace
{
CallLogModel* g_activeCallLogModel = nullptr;

void OnEventFromHook(void* userData, const Engine::Services::CallLogEvent& event) {
    auto* model = static_cast<CallLogModel*>(userData);
    if (model) {
        model->PushEvent(event);
    }
}

std::string FormatEventLine(const Engine::Services::CallLogEvent& event) {
    char line[1024] = {};
    size_t linePos = snprintf(line,
                              sizeof(line),
                              "[%s] %s(",
                              event.timeLabel[0] != '\0' ? event.timeLabel : "?",
                              event.displayLabel[0] != '\0' ? event.displayLabel : "?");

    size_t argsWritten = 0;
    const uint8_t maxArgs = event.isStatic ? 4 : 3;
    const uint8_t argCount = static_cast<uint8_t>((std::min)(
        static_cast<size_t>(event.paramCount),
        static_cast<size_t>((std::min)(maxArgs, static_cast<uint8_t>(Engine::Services::kCallLogMaxParamTypes)))));

    for (uint8_t i = 0; i < argCount && i < Engine::Services::kCallLogMaxParamTypes; ++i) {
        const int regIndex = event.isStatic ? static_cast<int>(i) : static_cast<int>(i) + 1;
        if (regIndex < 0 || regIndex > 3) {
            continue;
        }

        char argName[16] = {};
        snprintf(argName, sizeof(argName), "arg%u", i);

        const std::string typeName = event.paramTypeNames[i];
        const std::string decoded =
            Engine::Decode::DecodeRegisterArgument(typeName, event.argRegisters[regIndex]);

        const int added = snprintf(line + linePos,
                                   sizeof(line) - linePos,
                                   argsWritten == 0 ? "%s: %s" : ", %s: %s",
                                   argName,
                                   decoded.c_str());
        if (added > 0) {
            linePos += static_cast<size_t>(added);
            ++argsWritten;
        }
    }

    if (linePos < sizeof(line) - 2) {
        strncat_s(line, sizeof(line), ")", _TRUNCATE);
    }
    return line;
}
} // namespace

void CallLogModel::EnsureLineCallbackRegistered() {
    if (callbackRegistered) {
        g_activeCallLogModel = this;
        return;
    }
    g_activeCallLogModel = this;
    Engine::Services::CallLogHooks::SetEventCallback(OnEventFromHook, this);
    callbackRegistered = true;
}

void CallLogModel::PushEvent(const Engine::Services::CallLogEvent& event) {
    std::lock_guard<std::mutex> lock(mutex);
    pendingEvents.push_back(event);
    while (pendingEvents.size() > kMaxLines) {
        pendingEvents.pop_front();
    }
}

void CallLogModel::PushLine(const char* line) {
    if (!line || line[0] == '\0') {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    lines.push_back(line);
    while (lines.size() > kMaxLines) {
        lines.pop_front();
    }
}

void CallLogModel::ClearLines() {
    std::lock_guard<std::mutex> lock(mutex);
    lines.clear();
    pendingEvents.clear();
}

std::vector<std::string> CallLogModel::SnapshotLines() const {
    std::deque<Engine::Services::CallLogEvent> events;
    {
        std::lock_guard<std::mutex> lock(mutex);
        events.swap(pendingEvents);
    }

    if (!events.empty()) {
        std::deque<std::string> formatted;
        for (const auto& event : events) {
            formatted.push_back(FormatEventLine(event));
        }

        std::lock_guard<std::mutex> lock(mutex);
        for (auto& line : formatted) {
            lines.push_back(std::move(line));
        }
        while (lines.size() > kMaxLines) {
            lines.pop_front();
        }
    }

    std::lock_guard<std::mutex> lock(mutex);
    return std::vector<std::string>(lines.begin(), lines.end());
}

bool CallLogModel::IsLogging(uintptr_t target) const {
    if (target == 0) {
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& hook : hooks) {
        if (hook.target == target) {
            return true;
        }
    }
    return false;
}

bool CallLogModel::IsLogEligible(const Engine::MethodInfo& method) {
    if (method.address == 0) {
        return false;
    }
    if (!Engine::Memory::IsExecutablePointer(reinterpret_cast<const void*>(method.address))) {
        return false;
    }
    if (!method.paramsKnown && !method.paramTypes.empty()) {
        return false;
    }

    for (const auto& p : method.paramTypes) {
        if (Engine::Dumper::ClassifyInvokeParam(p) == Engine::Dumper::InvokeParamSupport::Unsupported) {
            return false;
        }
    }

    const size_t paramCount = method.paramTypes.size();
    if (method.isStatic) {
        return paramCount <= 4;
    }
    return paramCount <= 3;
}

CallLogModel::ToggleResult CallLogModel::Toggle(const Engine::MethodInfo& method,
                                                const std::string& className) {
    EnsureLineCallbackRegistered();

    if (!IsLogEligible(method)) {
        return ToggleResult::RejectedIneligible;
    }

    if (IsLogging(method.address)) {
        uint32_t hookId = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (const auto& hook : hooks) {
                if (hook.target == method.address) {
                    hookId = hook.hookId;
                    break;
                }
            }
        }
        if (hookId != 0 && RemoveHook(hookId)) {
            return ToggleResult::Removed;
        }
        return ToggleResult::RejectedInstallFailed;
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        if (hooks.size() >= kMaxHooks) {
            return ToggleResult::RejectedCap;
        }
    }

    if (Engine::Services::CallLogHooks::IsTargetHooked(method.address)) {
        return ToggleResult::RejectedAlreadyHooked;
    }

    Engine::Services::CallLogHookSpec spec{};
    spec.hookId     = nextHookId++;
    spec.target     = method.address;
    spec.className  = className.empty() ? "<class>" : className;
    spec.methodName = method.name;
    spec.displayLabel =
        Engine::Services::MakeCallLogDisplayLabel(spec.className, spec.methodName);
    spec.isStatic   = method.isStatic;
    spec.paramTypes = method.paramTypes;

    const auto installResult = Engine::Services::CallLogHooks::Install(spec);
    switch (installResult) {
    case Engine::Services::CallLogHooks::InstallResult::Ok:
        break;
    case Engine::Services::CallLogHooks::InstallResult::TargetAlreadyHooked:
        return ToggleResult::RejectedAlreadyHooked;
    case Engine::Services::CallLogHooks::InstallResult::NoFreeSlot:
        return ToggleResult::RejectedCap;
    default:
        return ToggleResult::RejectedInstallFailed;
    }

    std::lock_guard<std::mutex> lock(mutex);
    hooks.push_back(spec);
    return ToggleResult::Added;
}

bool CallLogModel::RemoveHook(uint32_t hookId) {
  if (hookId == 0) {
      return false;
  }
  if (!Engine::Services::CallLogHooks::Uninstall(hookId)) {
      return false;
  }
  std::lock_guard<std::mutex> lock(mutex);
  hooks.erase(std::remove_if(hooks.begin(),
                             hooks.end(),
                             [hookId](const Engine::Services::CallLogHookSpec& h) {
                                 return h.hookId == hookId;
                             }),
              hooks.end());
  return true;
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
