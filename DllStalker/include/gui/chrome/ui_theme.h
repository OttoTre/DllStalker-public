#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include "imgui.h"

#include "gui/chrome/ui_icons.h"

namespace Gui::UiTheme
{
// Panels read Tokens() for colours — do not hardcode hex in views.
struct ColorTokens
{
    ImVec4 accent{};
    ImVec4 accent_muted{};
    ImVec4 text_object{};
    ImVec4 semantic_link{};
    ImVec4 semantic_link_hover{};
    ImVec4 semantic_link_active{};
    ImVec4 semantic_struct{};
    ImVec4 success{};
    ImVec4 warning{};
    ImVec4 error{};
    ImVec4 bookmark_gold{};
    ImVec4 chip_border{};

    ImVec4 bg0{};
    ImVec4 bg1{};
    ImVec4 bg2{};
    ImVec4 bg3{};
    ImVec4 bg4{};
    ImVec4 border{};
    ImVec4 text{};
    ImVec4 text_disabled{};
    ImVec4 popup{};
    ImVec4 table_header{};
};

void LoadFonts();
void ApplyDllStalkerTheme();
const ColorTokens& Tokens();

bool BeginUnderlineTabBar(const char* id);
void EndUnderlineTabBar();
void PushUnderlineTabStyle();
void PopUnderlineTabStyle();

bool PrimaryButton(const char* label, const ImVec2& size = ImVec2(0, 0));

bool CenteredGlyphButton(const char* id, const char* glyph, float width,
                         const ImVec4* text_color = nullptr);
void CenteredDisabledGlyph(const char* glyph, float width);

void DrawColumnName(const char* text);
void DrawColumnType(const char* text);
void DrawColumnOffset(const char* text);
void DrawColumnValue(const char* text);

// Caller must PushID() for unique row ids.
// minWidth: optional content floor so ListClipper + HorizontalScrollbar stay stable
// (pass max label width for the list; 0 = pane/label only).
bool BrowseSelectable(const char* display, bool selected, float indent = 0.0f,
                      float minWidth = 0.0f);

bool ElevatedFilter(const char* id, char* buf, size_t buf_size, float width = -1.0f,
                    const char* hint = nullptr);
bool GhostButton(const char* label, const ImVec4* text_color = nullptr);
bool ChipToggle(const char* label, bool* value);
// Umbra '=' / '~' match-mode toggle.
bool GhostFilterModeButton(const char* id, bool* isStrict,
                           const char* tooltipStrict, const char* tooltipFuzzy);

ImGuiTableFlags InspectorTableFlags();
bool BeginInspectorTable(const char* id, int columns);
void EndInspectorTable();

void DrawMutedFooter(const char* text);
void DrawSuccessText(const char* text);
void DrawWarningText(const char* text);
void DrawErrorText(const char* text);
} // namespace Gui::UiTheme

#endif // ENABLE_DUMPER
