#pragma once

#include "pch.h"

#include <string>
#include <vector>

#include "gui/state/walker_controller.h"

namespace Gui::State
{
struct NavigationSnapshot {
    void* imagePtr = nullptr;
    std::string imageName{};
    void* classPtr = nullptr;
    std::string className{};
    void* instancePtr = nullptr;
    int   instanceIndex = -1;
    std::vector<InspectorBreadcrumb> breadcrumbs{};
    std::string summaryLabel{};
};

// Location-only label for bookmarks (no action prefix). Rebuilds from stored
// snapshot fields; ignores summaryLabel ("Bookmark @ …").
inline std::string NavigationLocationLabel(const NavigationSnapshot& snap) {
    std::string base;
    if (!snap.className.empty() && snap.className != "<class>") {
        base = snap.className;
    }
    else if (!snap.imageName.empty() && snap.imageName != "<image>") {
        base = snap.imageName;
    }

    if (!snap.breadcrumbs.empty()) {
        const auto& top = snap.breadcrumbs.back();
        if (!top.label.empty()) {
            if (!base.empty()) {
                return base + " @ " + top.label;
            }
            return top.label;
        }
    }
    return base;
}

inline bool NavigationFingerprintsEqual(const NavigationSnapshot& a, const NavigationSnapshot& b) {
    if (a.classPtr != b.classPtr || a.instancePtr != b.instancePtr) {
        return false;
    }
    if (a.breadcrumbs.size() != b.breadcrumbs.size()) {
        return false;
    }
    for (size_t i = 0; i < a.breadcrumbs.size(); ++i) {
        if (a.breadcrumbs[i].label != b.breadcrumbs[i].label) {
            return false;
        }
        if (a.breadcrumbs[i].isCollection != b.breadcrumbs[i].isCollection) {
            return false;
        }
    }
    return true;
}

enum class HistoryRestoreResult {
    Applied,
    StaleInstance,
    StaleBreadcrumb,
    InvalidEntryKind,
    BookmarkNotFound,
    DumperUnavailable,
};

enum class BreadcrumbValidationResult {
    Valid,
    StaleInstance,
    StaleBreadcrumb,
};
} // namespace Gui::State
