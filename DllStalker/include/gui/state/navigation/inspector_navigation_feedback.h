#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

namespace Gui::State
{
// Shared inspector navigation UX state: status toasts after restore/bookmark
// apply, and the async-instance history dedupe gate (lastAsyncRecordedInstance).
// Used by History and Bookmarks tabs and by TryApplyNavigationSnapshot.
struct InspectorNavigationFeedback {
    static constexpr double kStatusNeverShown = -1000.0;

    char   statusMessage[256] = "";
    double statusUpdatedAtSec = kStatusNeverShown;

    // Suppresses a duplicate "Instance search result" history row when
    // activeInstancePtr is set without going through SelectInstanceByIndex.
    void* lastAsyncRecordedInstance = nullptr;

    void MarkStatus(const char* message, double nowSeconds);

    bool IsStatusFresh(double nowSeconds, double maxAgeSeconds = 3.0) const;
};
} // namespace Gui::State

#endif // ENABLE_DUMPER
