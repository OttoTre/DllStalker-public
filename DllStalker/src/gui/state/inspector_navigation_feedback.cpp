#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/inspector_navigation_feedback.h"

namespace Gui::State
{
void InspectorNavigationFeedback::MarkStatus(const char* message, double nowSeconds) {
    if (!message) {
        statusMessage[0] = '\0';
    }
    else {
        strncpy_s(statusMessage, sizeof(statusMessage), message, _TRUNCATE);
    }
    statusUpdatedAtSec = nowSeconds;
}

bool InspectorNavigationFeedback::IsStatusFresh(double nowSeconds, double maxAgeSeconds) const {
    return statusMessage[0] != '\0'
           && (nowSeconds - statusUpdatedAtSec) < maxAgeSeconds;
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
