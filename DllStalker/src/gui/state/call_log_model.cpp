#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/call_log_model.h"

#include "services/call_log_hooks.h"

#include <algorithm>

#include "types/type_classifier.h"

namespace Gui::State
{
namespace
{
CallLogModel* g_activeCallLogModel = nullptr;

void OnLineFromHook(void* userData, const char* line) {
    auto* model = static_cast<CallLogModel*>(userData);
    if (model && line) {
        model->PushLine(line);
    }
}
} // namespace

void CallLogModel::EnsureLineCallbackRegistered() {
    if (callbackRegistered) {
        g_activeCallLogModel = this;
        return;
    }
    g_activeCallLogModel = this;
    Engine::Services::CallLogHooks::SetLineCallback(OnLineFromHook, this);
    callbackRegistered = true;
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
}

std::vector<std::string> CallLogModel::SnapshotLines() const {
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
    if (!method.paramsKnown && !method.paramTypes.empty()) {
        return false;
    }

    using Cat = Engine::Types::TypeCategory;
    for (const auto& p : method.paramTypes) {
        const Cat cat = Engine::Types::GetCategory(p.typeName);
        switch (cat) {
        case Cat::I1:
        case Cat::I2:
        case Cat::I4:
        case Cat::I8:
        case Cat::U1:
        case Cat::U2:
        case Cat::U4:
        case Cat::U8:
        case Cat::R4:
        case Cat::R8:
        case Cat::BOOLEAN:
        case Cat::STRING:
        case Cat::PTR:
            continue;
        default:
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
