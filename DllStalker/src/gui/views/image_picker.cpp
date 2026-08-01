#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/image_picker.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"

#include "imgui.h"

#include <cstdio>
#include <string>
#include <vector>

namespace Gui::Views
{
void RenderImageSelection(ControlPanelSessionState& state) {
    ImGui::SeparatorText("Image Selection");
    UiTheme::ElevatedFilter("##image_filter", state.imageFilterBuffer, sizeof(state.imageFilterBuffer),
                            -1.0f, "Filter...");

    std::vector<Engine::ImageInfo> imageCacheSnapshot = state.GetImageCacheSnapshot();
    const char* activeImageLabel = state.imgSearchBuffer[0] ? state.imgSearchBuffer : "Select image...";

    if (state.loaders.imageLoadInProgress.load()) {
        ImGui::TextUnformatted("Loading images...");
    }

    const float refreshSize = ImGui::GetFrameHeight();
    const float refreshGap = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - refreshSize - refreshGap);
    if (ImGui::BeginCombo("##active_image", activeImageLabel)) {
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

    ImGui::SameLine(0.0f, refreshGap);
    const bool refreshBusy = state.loaders.imageLoadInProgress.load();
    if (refreshBusy) {
        ImGui::BeginDisabled();
    }
    if (UiTheme::IconRefreshButton("##refresh_images", "Refresh images", refreshSize) && !refreshBusy) {
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
    if (refreshBusy) {
        ImGui::EndDisabled();
    }

    if (state.selectedImage) {
        char attached[288] = {};
        std::snprintf(attached, sizeof(attached), "Attached: %s", state.imgSearchBuffer);
        UiTheme::DrawSuccessText(attached);
    }
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
