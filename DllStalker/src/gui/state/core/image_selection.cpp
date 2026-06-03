#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/session_state.h"

#include <cstring>
#include <string>

namespace Gui
{
void ControlPanelSessionState::SelectImage(const Engine::ImageInfo& img, bool recordHistory) {
    // The combo's closed label reads from imgSearchBuffer, so the cosmetic
    // hint set at default-init ("Assembly-CSharp") gets replaced with the
    // canonical cache entry ("Assembly-CSharp.dll" etc.) the moment a real
    // selection takes hold -- whether via manual click or the first-load
    // reconciler in app_shell.cpp.
    strncpy_s(imgSearchBuffer, sizeof(imgSearchBuffer), img.name.c_str(), _TRUNCATE);

    selectedImage = img.imagePtr;
    selectedClass = nullptr;

    ClearClassCache();
    ClearInspectorCache();

    // StartClassLoad cancels any in-flight class worker via std::jthread
    // move-assignment, so callers don't need explicit join helpers.
    if (selectedImage && dumper) {
        StartClassLoad(dumper, selectedImage);
    }

    if (recordHistory) {
        const std::string historyLabel = "Select image: " + img.name;
        RecordNavigationEvent(historyLabel.c_str());
    }
}
} // namespace Gui

#endif // ENABLE_DUMPER
