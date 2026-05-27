#include "pch.h"

#ifdef ENABLE_DUMPER

#include "services/call_log_types.h"

namespace Engine::Services
{
std::string MakeCallLogDisplayLabel(const std::string& className, const std::string& methodName) {
    if (methodName.find("::") != std::string::npos) {
        return methodName;
    }
    if (className.empty() || className == "<class>") {
        return methodName;
    }
    return className + "::" + methodName;
}

std::string CallLogHookSpec::DisplayLabel() const {
    if (!displayLabel.empty()) {
        return displayLabel;
    }
    return MakeCallLogDisplayLabel(className, methodName);
}
} // namespace Engine::Services

#endif // ENABLE_DUMPER
