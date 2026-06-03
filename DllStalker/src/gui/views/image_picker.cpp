#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/image_picker.h"

#include "gui/session_state.h"

#include "imgui.h"

#include <string>
#include <vector>

namespace Gui::Views
{
void RenderImageSelection(ControlPanelSessionState& state) {
    ImGui::SeparatorText("Image Selection");
    ImGui::InputText("Image Filter", state.imageFilterBuffer, sizeof(state.imageFilterBuffer));

    std::vector<Engine::ImageInfo> imageCacheSnapshot = state.GetImageCacheSnapshot();
    const char* activeImageLabel = state.imgSearchBuffer[0] ? state.imgSearchBuffer : "Select image...";

    if (state.loaders.imageLoadInProgress.load()) {
        ImGui::TextUnformatted("Loading images...");
    }

    if (ImGui::BeginCombo("Active Image", activeImageLabel)) {
        for (const auto& img : imageCacheSnapshot) {
            if (state.imageFilterBuffer[0] != '\0' && strstr(img.name.c_str(), state.imageFilterBuffer) == nullptr) {
                continue;
            }

            bool isSelected = (state.selectedImage == img.imagePtr);
            std::string rowLabel = img.name + " [" + std::to_string(img.classCount) + " classes]";
            if (ImGui::Selectable(rowLabel.c_str(), isSelected)) {
                // Shared path with the app-shell first-load reconciler so
                // manual and auto selection clear the same caches and emit
                // the same history row.
                state.SelectImage(img);
            }

            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Refresh Images", ImVec2(-1, 0)) && !state.loaders.imageLoadInProgress.load()) {
        state.ClearImageCache();
        state.StartImageLoad(state.dumper);
        // If nothing is selected yet, re-arm the first-load reconciler so
        // the refreshed cache gets the same default-hint resolution that
        // the original load got. Don't disturb an existing selection --
        // re-resolving a stale selectedImage by name is a separate concern.
        if (!state.selectedImage) {
            state.pendingDefaultImageSelection = true;
        }
    }

    if (state.selectedImage) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Attached: %s", state.imgSearchBuffer);
    }
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
