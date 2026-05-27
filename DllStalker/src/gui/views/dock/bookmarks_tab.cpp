#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/dock/bookmarks_tab.h"

#include "gui/views/dock/navigation_status_banner.h"

#include "gui/state/history_steady_time.h"
#include "gui/state/inspector_navigation_snapshot.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Gui::Views
{
namespace
{
constexpr size_t kBookmarkNameBufferSize = 128;

struct BookmarksTabModalState {
    bool     openCreateRequested = false;
    bool     openRenameRequested = false;
    uint32_t pendingRenameId     = 0;
    bool     showInlineNameError = false;

    char createNameBuffer[kBookmarkNameBufferSize] = "";
    char renameNameBuffer[kBookmarkNameBufferSize] = "";
};

BookmarksTabModalState& ModalState() {
    static BookmarksTabModalState s_state;
    return s_state;
}

void CopyTruncated(char* dst, size_t dstSize, const char* src) {
    if (dstSize == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy_s(dst, dstSize, src, _TRUNCATE);
}

std::string DefaultBookmarkName(const ControlPanelSessionState& state) {
    const auto snap = state.CaptureNavigationSnapshot("");
    return State::NavigationLocationLabel(snap);
}

float MeasureSmallButtonWidth(const char* label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
}

void RenderBookmarkRow(ControlPanelSessionState& state,
                       BookmarksTabModalState& modal,
                       const Gui::State::Bookmark& entry,
                       uint32_t& deleteRequestedId) {
    const std::string label =
        entry.name.empty() ? std::string("(unnamed)") : entry.name;
    const std::string subtitle =
        State::NavigationLocationLabel(entry.snapshot);

    const ImGuiStyle& style  = ImGui::GetStyle();
    const float       yRow   = ImGui::GetCursorPosY();
    const float       xStart = ImGui::GetCursorPosX();
    const float       availX = ImGui::GetContentRegionAvail().x;

    const float deleteW  = MeasureSmallButtonWidth("Delete");
    const float renameW  = MeasureSmallButtonWidth("Rename");
    const float actionsW = deleteW + renameW + style.ItemInnerSpacing.x;

    float subtitleW = 0.0f;
    if (!subtitle.empty()) {
        subtitleW = ImGui::CalcTextSize((" - " + subtitle).c_str()).x
                    + style.ItemInnerSpacing.x;
    }

    const float leftW       = (std::max)(availX - actionsW, 1.0f);
    const float selectableW = (std::max)(leftW - subtitleW, 1.0f);

    ImGui::SetCursorPos(ImVec2(xStart + availX - actionsW, yRow));
    if (ImGui::SmallButton("Rename")) {
        modal.pendingRenameId = entry.id;
        CopyTruncated(modal.renameNameBuffer, sizeof(modal.renameNameBuffer),
                      entry.name.c_str());
        modal.showInlineNameError   = false;
        modal.openRenameRequested = true;
    }
    ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
    if (ImGui::SmallButton("Delete")) {
        deleteRequestedId = entry.id;
    }

    ImGui::SetCursorPos(ImVec2(xStart, yRow));
    if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(selectableW, 0.0f))) {
        state.TryApplyBookmark(entry.id);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Click to restore this bookmark.");
    }

    if (!subtitle.empty()) {
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextDisabled(" - %s", subtitle.c_str());
    }

    ImGui::SetCursorPos(
        ImVec2(xStart, yRow + ImGui::GetFrameHeight() + style.ItemSpacing.y));
}

void RenderCreatePopup(ControlPanelSessionState& state) {
    auto& modal = ModalState();
    if (!ImGui::BeginPopupModal("BookmarksCreatePopup", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextUnformatted("Bookmark current view");
    ImGui::Separator();

    ImGui::SetNextItemWidth(280.0f);
    ImGui::InputText("Name##bookmarkCreate",
                     modal.createNameBuffer, sizeof(modal.createNameBuffer));

    if (modal.showInlineNameError) {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                           "Name cannot be empty.");
    }

    ImGui::Separator();
    if (ImGui::Button("Save", ImVec2(120, 0))) {
        if (state.BookmarkCurrentView(modal.createNameBuffer)) {
            modal.createNameBuffer[0]   = '\0';
            modal.showInlineNameError   = false;
            ImGui::CloseCurrentPopup();
        }
        else {
            modal.showInlineNameError = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        modal.createNameBuffer[0]   = '\0';
        modal.showInlineNameError   = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void RenderRenamePopup(ControlPanelSessionState& state) {
    auto& modal = ModalState();
    if (!ImGui::BeginPopupModal("BookmarksRenamePopup", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    auto* entry = state.bookmarks.Find(modal.pendingRenameId);
    if (!entry) {
        modal.pendingRenameId        = 0;
        modal.renameNameBuffer[0]    = '\0';
        modal.showInlineNameError    = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted("Rename bookmark");
    ImGui::Separator();

    ImGui::SetNextItemWidth(280.0f);
    ImGui::InputText("Name##bookmarkRename",
                     modal.renameNameBuffer, sizeof(modal.renameNameBuffer));

    if (modal.showInlineNameError) {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                           "Name cannot be empty.");
    }

    ImGui::Separator();
    if (ImGui::Button("Save", ImVec2(120, 0))) {
        if (state.bookmarks.Rename(modal.pendingRenameId, modal.renameNameBuffer)) {
            modal.pendingRenameId        = 0;
            modal.renameNameBuffer[0]    = '\0';
            modal.showInlineNameError    = false;
            ImGui::CloseCurrentPopup();
        }
        else {
            modal.showInlineNameError = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        modal.pendingRenameId        = 0;
        modal.renameNameBuffer[0]    = '\0';
        modal.showInlineNameError    = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
} // namespace

void RenderBookmarksTab(ControlPanelSessionState& state) {
    auto& modal = ModalState();

    const bool canBookmark = state.selectedClass != nullptr;

    if (!canBookmark) ImGui::BeginDisabled();
    const bool createClicked = ImGui::Button("Bookmark current...");
    if (!canBookmark) ImGui::EndDisabled();
    if (!canBookmark && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Select a class first.");
    }
    if (createClicked) {
        const std::string preset = DefaultBookmarkName(state);
        CopyTruncated(modal.createNameBuffer, sizeof(modal.createNameBuffer),
                      preset.c_str());
        modal.showInlineNameError = false;
        modal.openCreateRequested = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear all##bookmarks")) {
        state.bookmarks.Clear();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%zu bookmark%s", state.bookmarks.Size(),
                        state.bookmarks.Size() == 1 ? "" : "s");

    RenderNavigationStatusBanner(state);

    if (state.bookmarks.bookmarks.empty()) {
        ImGui::TextUnformatted("No bookmarks yet. Use \"Bookmark current...\" to save the active inspector view.");
    }

    if (!state.bookmarks.bookmarks.empty()) {
        if (!ImGui::BeginChild("BookmarksList", ImVec2(0, 0), true)) {
            // Don't return -- popups still need to render below.
        }
        else {
            std::vector<uint32_t> ids;
            ids.reserve(state.bookmarks.bookmarks.size());
            for (const auto& b : state.bookmarks.bookmarks) {
                ids.push_back(b.id);
            }

            uint32_t deleteRequestedId = 0;

            for (uint32_t id : ids) {
                auto* entry = state.bookmarks.Find(id);
                if (!entry) continue;

                ImGui::PushID(static_cast<int>(id));
                RenderBookmarkRow(state, modal, *entry, deleteRequestedId);
                ImGui::PopID();
            }

            if (deleteRequestedId != 0) {
                state.bookmarks.Remove(deleteRequestedId);
            }
        }
        ImGui::EndChild();
    }

    if (modal.openCreateRequested) {
        ImGui::OpenPopup("BookmarksCreatePopup");
        modal.openCreateRequested = false;
    }
    if (modal.openRenameRequested) {
        ImGui::OpenPopup("BookmarksRenamePopup");
        modal.openRenameRequested = false;
    }

    RenderCreatePopup(state);
    RenderRenamePopup(state);
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
