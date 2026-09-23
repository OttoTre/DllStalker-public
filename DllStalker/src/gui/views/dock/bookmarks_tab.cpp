#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/dock/bookmarks_tab.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"
#include "gui/views/dock/navigation_status_banner.h"

#include "gui/state/navigation/history_steady_time.h"
#include "gui/state/navigation/inspector_navigation_snapshot.h"

#include "imgui.h"

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
    bool     openRenameRequested = false;
    uint32_t pendingRenameId     = 0;
    bool     showInlineNameError = false;

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

float MeasureSmallButtonWidth(const char* label) {
    const ImGuiStyle& style = ImGui::GetStyle();
    return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
}

float BookmarkActionsColumnWidth() {
    const ImGuiStyle& style = ImGui::GetStyle();
    return MeasureSmallButtonWidth("Rename")
           + MeasureSmallButtonWidth("Delete")
           + style.ItemInnerSpacing.x
           + style.CellPadding.x * 2.0f;
}

void RenderBookmarkRow(ControlPanelSessionState& state,
                       BookmarksTabModalState& modal,
                       const Gui::State::Bookmark& entry,
                       uint32_t& deleteRequestedId) {
    const std::string label =
        entry.name.empty() ? std::string("(unnamed)") : entry.name;
    const std::string location =
        State::NavigationLocationLabel(entry.snapshot);
    const ImGuiStyle& style = ImGui::GetStyle();
    ImFont*           font  = ImGui::GetFont();
    const float       fontSize = ImGui::GetFontSize();
    const float       lineH = ImGui::GetTextLineHeight();
    const float       rowH  = lineH * 2.0f;

    ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
    ImGui::TableSetColumnIndex(0);
    if (ImGui::Selectable("##bookmark", false, 0,
                          ImVec2(ImGui::GetContentRegionAvail().x,
                                 rowH))) {
        state.TryApplyBookmark(entry.id);
    }
    const ImVec2 rowMin = ImGui::GetItemRectMin();
    const ImVec2 rowMax = ImGui::GetItemRectMax();
    const float textX = rowMin.x + style.FramePadding.x;
    const float textY = rowMin.y;
    const ImVec4 textClip(textX,
                          rowMin.y,
                          rowMax.x - style.FramePadding.x,
                          rowMax.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddText(font, fontSize, ImVec2(textX, textY),
                      ImGui::GetColorU32(ImGuiCol_Text),
                      label.c_str(), nullptr, 0.0f, &textClip);
    if (!location.empty()) {
        drawList->AddText(font, fontSize, ImVec2(textX, textY + lineH),
                          ImGui::GetColorU32(ImGuiCol_TextDisabled),
                          location.c_str(), nullptr, 0.0f, &textClip);
    }
    if (ImGui::IsItemHovered()) {
        const std::string& imageName = entry.snapshot.imageName;
        const bool hasImage = !imageName.empty() && imageName != "<image>";
        std::string tooltip;
        if (hasImage) {
            tooltip = "Image: ";
            tooltip += imageName;
        }
        if (!location.empty()) {
            if (!tooltip.empty()) {
                tooltip += '\n';
            }
            tooltip += location;
        }
        if (!tooltip.empty()) {
            tooltip += "\n\n";
        }
        tooltip += "Click to restore this bookmark.";
        ImGui::SetTooltip("%s", tooltip.c_str());
    }

    ImGui::TableSetColumnIndex(1);
    ImVec2 actionPos = ImGui::GetCursorScreenPos();
    const float smallButtonH = ImGui::CalcTextSize("Rename").y;
    actionPos.y = rowMin.y + (rowH - smallButtonH) * 0.5f;
    ImGui::SetCursorScreenPos(actionPos);
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
        UiTheme::DrawErrorText("Name cannot be empty.");
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

    if (ImGui::Button("Clear all##bookmarks")) {
        state.bookmarks.Clear();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%zu bookmark%s", state.bookmarks.Size(),
                        state.bookmarks.Size() == 1 ? "" : "s");

    RenderNavigationStatusBanner(state, true);

    if (state.bookmarks.bookmarks.empty()) {
        ImGui::TextUnformatted("No bookmarks yet. Use the star in the Inspector header to save the active view.");
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

            const ImGuiStyle& style = ImGui::GetStyle();
            ImGui::PushStyleVar(
                ImGuiStyleVar_CellPadding,
                ImVec2(style.CellPadding.x, 0.0f));
            ImGui::PushStyleVar(
                ImGuiStyleVar_ItemSpacing,
                ImVec2(style.ItemSpacing.x, 0.0f));
            ImGui::PushStyleColor(
                ImGuiCol_TableRowBgAlt,
                ImVec4(1.0f, 1.0f, 1.0f, 0.04f));
            ImGui::PushStyleColor(
                ImGuiCol_TableBorderLight,
                ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
            if (ImGui::BeginTable("BookmarksTable", 2,
                                  ImGuiTableFlags_SizingStretchProp
                                      | ImGuiTableFlags_NoSavedSettings
                                      | ImGuiTableFlags_RowBg
                                      | ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn(
                    "Bookmark", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn(
                    "Actions", ImGuiTableColumnFlags_WidthFixed,
                    BookmarkActionsColumnWidth());

                for (uint32_t id : ids) {
                    auto* entry = state.bookmarks.Find(id);
                    if (!entry) continue;

                    ImGui::PushID(static_cast<int>(id));
                    RenderBookmarkRow(state, modal, *entry, deleteRequestedId);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

            if (deleteRequestedId != 0) {
                state.bookmarks.Remove(deleteRequestedId);
            }
        }
        ImGui::EndChild();
    }

    if (modal.openRenameRequested) {
        ImGui::OpenPopup("BookmarksRenamePopup");
        modal.openRenameRequested = false;
    }

    RenderRenamePopup(state);
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
