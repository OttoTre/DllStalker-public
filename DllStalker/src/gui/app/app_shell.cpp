#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/app/app_shell.h"

#include "gui/session_state.h"
#include "gui/config.h"
#include "gui/views/class_browser.h"
#include "gui/views/dock/utilities_dock.h"
#include "gui/views/error_modals.h"
#include "gui/views/image_picker.h"
#include "gui/views/inspector_frame.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace Gui::AppShell
{
namespace
{
// Pick the first image whose name matches the user-visible hint. Exact
// match wins over substring so a configured "mscorlib" hint binds to
// "mscorlib" rather than "mscorlib.dll" if both happen to be present.
// Substring fallback mirrors Engine::ImageEnumerator::FindImage's
// strstr() so "Assembly-CSharp" still resolves to "Assembly-CSharp.dll".
const Engine::ImageInfo* FindImageMatchingHint(
    const std::vector<Engine::ImageInfo>& images,
    const char* hint)
{
    if (!hint || hint[0] == '\0') {
        return nullptr;
    }
    for (const auto& img : images) {
        if (img.name == hint) {
            return &img;
        }
    }
    for (const auto& img : images) {
        if (strstr(img.name.c_str(), hint) != nullptr) {
            return &img;
        }
    }
    return nullptr;
}
} // namespace

void BeginControlPanelFrame(HWND hwnd) {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    RECT rect;
    GetClientRect(hwnd, &rect);
    const float width = (float)(rect.right - rect.left);
    const float height = (float)(rect.bottom - rect.top);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(width, height));
}

void RenderControlPanelContent(ControlPanelSessionState& state) {
    static Views::CopyFeedbackState copyFeedback{};

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                                   | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
    ImGui::Begin("DllStalker", nullptr, window_flags);

    if (!state.dumper) {
        RenderDumperInitialization(state);
    }
    else {
        RenderMainLayout(state, copyFeedback);
    }

    Views::RenderErrorPopups();

    ImGui::End();
}

void RenderMainLayout(ControlPanelSessionState& state, Views::CopyFeedbackState& copyFeedback) {
    if (!state.loaders.imageLoadInProgress.load() && state.GetImageCacheSnapshot().empty()) {
        state.StartImageLoad(state.dumper);
    }

    // First-load reconciler: the image combo's closed-label shows
    // imgSearchBuffer ("Assembly-CSharp" by default) immediately, but the
    // manual click handler is the only path that actually sets
    // selectedImage and kicks off StartClassLoad. Bridge that gap once the
    // image cache is populated so the user doesn't have to re-pick the
    // image the UI is already pretending to have selected. Cleared after
    // one attempt -- a missing hint shouldn't scan every frame.
    if (state.pendingDefaultImageSelection
        && !state.loaders.imageLoadInProgress.load()
        && state.selectedImage == nullptr
        && state.dumper
        && state.imgSearchBuffer[0] != '\0') {
        const auto images = state.GetImageCacheSnapshot();
        if (!images.empty()) {
            if (const Engine::ImageInfo* match = FindImageMatchingHint(images, state.imgSearchBuffer)) {
                state.SelectImage(*match, /*recordHistory=*/true);
            }
            state.pendingDefaultImageSelection = false;
        }
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float minMainH = 200.0f * Config::GUI_SCALE;
    const float minDockH = 160.0f * Config::GUI_SCALE;

    // Stacked BeginChild calls consume ItemSpacing between siblings; the dock
    // child uses border=true (ChildBorderSize top+bottom). Without reserving
    // that chrome, mainH + dockH slightly exceeds avail and the root window
    // gets a ~few-pixel vertical scrollbar (worse after ScaleAllSizes).
    const ImGuiStyle& style = ImGui::GetStyle();
    const float layoutChrome = style.ItemSpacing.y + style.ChildBorderSize * 2.0f;
    const float layoutH = (std::max)(0.0f, avail.y - layoutChrome);

    float dockH = (std::max)(minDockH, layoutH * 0.35f);
    float mainH = layoutH - dockH;
    if (mainH < minMainH) {
        mainH = (std::min)(minMainH, layoutH);
        dockH = (std::max)(0.0f, layoutH - mainH);
    }

    ImGui::BeginChild("MainRegion", ImVec2(0, mainH), false);
    if (ImGui::BeginTable("MainLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Sidebar", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("Viewport", ImGuiTableColumnFlags_WidthStretch, 0.70f);

        ImGui::TableNextColumn();
        Views::RenderImageSelection(state);
        Views::RenderClassBrowser(state);

        ImGui::TableNextColumn();
        Views::RenderInspector(state, copyFeedback);

        ImGui::EndTable();
    }
    ImGui::EndChild();

    ImGui::BeginChild("UtilitiesDock", ImVec2(0, dockH), true);
    Views::RenderUtilitiesDock(state);
    ImGui::EndChild();
}

void RenderDumperInitialization(ControlPanelSessionState& state) {
    if (ImGui::Button("Init Dumper Engine", ImVec2(-1, 40))) {
        try {
            state.dumper = std::make_shared<Engine::UnityDumper>(Engine::Unity);
            state.ClearImageCache();
            state.StartImageLoad(state.dumper);
            // Re-arm the reconciler so a fresh Init Dumper after the user
            // shut things down still resolves the default hint.
            state.pendingDefaultImageSelection = true;
        }
        catch (...) { }
    }
}
} // namespace Gui::AppShell

#endif // ENABLE_DUMPER
