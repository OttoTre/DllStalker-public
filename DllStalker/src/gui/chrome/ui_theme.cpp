#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/chrome/ui_theme.h"

#include "gui/config.h"

#include "imgui.h"

#include <cfloat>

namespace Gui::UiTheme
{
namespace
{
ColorTokens g_tokens{};
bool g_tokensReady = false;

ImVec4 Hex(unsigned rgb, float a = 1.0f)
{
    return ImVec4(static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                  static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(rgb & 0xFF) / 255.0f, a);
}

ImVec4 WithAlpha(const ImVec4& c, float a)
{
    return ImVec4(c.x, c.y, c.z, a);
}

void InitTokens()
{
    // Charcoal matte + violet accent (#7C6BF0). Edit here to retheme.
    g_tokens.accent = Hex(0x7C6BF0);
    g_tokens.accent_muted = Hex(0x7C6BF0, 0.55f);
    g_tokens.text_object = Hex(0xE8D48A);
    g_tokens.semantic_link = Hex(0x8CB4F0);
    g_tokens.semantic_link_hover = Hex(0x8CB4F0, 0.18f);
    g_tokens.semantic_link_active = Hex(0x8CB4F0, 0.28f);
    g_tokens.semantic_struct = Hex(0x7BC47B);
    g_tokens.success = Hex(0x6FCF97);
    g_tokens.warning = Hex(0xF0A060);
    g_tokens.error = Hex(0xF25A5A);
    g_tokens.bookmark_gold = Hex(0xE8C547);
    g_tokens.chip_border = Hex(0x4A4E57, 0.90f);

    g_tokens.bg0 = Hex(0x16181C);
    g_tokens.bg1 = Hex(0x1C2026);
    g_tokens.bg2 = Hex(0x242930);
    g_tokens.bg3 = Hex(0x2E343C);
    g_tokens.bg4 = Hex(0x3A424C);
    g_tokens.border = Hex(0x454C58, 0.92f);
    g_tokens.text = Hex(0xF0F2F6);
    g_tokens.text_disabled = Hex(0x96A0AC);
    g_tokens.popup = Hex(0x20242A, 0.98f);
    g_tokens.table_header = Hex(0x222830);
    g_tokensReady = true;
}
} // namespace

void LoadFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    io.Fonts->AddFontDefault();
    io.FontGlobalScale = Config::GUI_SCALE;
}

void ApplyDllStalkerTheme()
{
    InitTokens();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;

    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(10.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.CellPadding = ImVec2(8.0f, 4.0f);
    style.IndentSpacing = 18.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;

    style.WindowTitleAlign = ImVec2(0.0f, 0.50f);
    style.ButtonTextAlign = ImVec2(0.50f, 0.50f);
    style.TabBarOverlineSize = 2.0f;

    const ImVec4& accent = g_tokens.accent;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = g_tokens.text;
    c[ImGuiCol_TextDisabled] = g_tokens.text_disabled;
    c[ImGuiCol_WindowBg] = g_tokens.bg1;
    c[ImGuiCol_ChildBg] = g_tokens.bg2;
    c[ImGuiCol_PopupBg] = g_tokens.popup;
    c[ImGuiCol_Border] = g_tokens.border;
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = g_tokens.bg3;
    c[ImGuiCol_FrameBgHovered] = g_tokens.bg4;
    c[ImGuiCol_FrameBgActive] = WithAlpha(accent, 0.22f);
    c[ImGuiCol_TitleBg] = g_tokens.bg0;
    c[ImGuiCol_TitleBgActive] = g_tokens.bg0;
    c[ImGuiCol_TitleBgCollapsed] = g_tokens.bg0;
    c[ImGuiCol_MenuBarBg] = g_tokens.bg2;
    c[ImGuiCol_ScrollbarBg] = WithAlpha(g_tokens.bg0, 0.65f);
    c[ImGuiCol_ScrollbarGrab] = Hex(0x4A505A);
    c[ImGuiCol_ScrollbarGrabHovered] = Hex(0x5A6270);
    c[ImGuiCol_ScrollbarGrabActive] = accent;
    c[ImGuiCol_CheckMark] = accent;
    c[ImGuiCol_SliderGrab] = accent;
    c[ImGuiCol_SliderGrabActive] = Hex(0x9A8CF5);
    c[ImGuiCol_Button] = g_tokens.bg3;
    c[ImGuiCol_ButtonHovered] = WithAlpha(accent, 0.35f);
    c[ImGuiCol_ButtonActive] = WithAlpha(accent, 0.55f);
    c[ImGuiCol_Header] = WithAlpha(accent, 0.18f);
    c[ImGuiCol_HeaderHovered] = WithAlpha(accent, 0.30f);
    c[ImGuiCol_HeaderActive] = WithAlpha(accent, 0.42f);
    c[ImGuiCol_Separator] = g_tokens.border;
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive] = accent;
    c[ImGuiCol_ResizeGrip] = WithAlpha(accent, 0.25f);
    c[ImGuiCol_ResizeGripHovered] = WithAlpha(accent, 0.45f);
    c[ImGuiCol_ResizeGripActive] = WithAlpha(accent, 0.70f);
    c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = WithAlpha(accent, 0.20f);
    c[ImGuiCol_TabActive] = WithAlpha(accent, 0.14f);
    c[ImGuiCol_TabUnfocused] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabUnfocusedActive] = WithAlpha(accent, 0.10f);
    c[ImGuiCol_TabSelectedOverline] = accent;
    c[ImGuiCol_TableHeaderBg] = g_tokens.table_header;
    c[ImGuiCol_TableBorderStrong] = g_tokens.border;
    c[ImGuiCol_TableBorderLight] = Hex(0x3A404A, 0.45f);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = Hex(0xFFFFFF, 0.02f);
    c[ImGuiCol_TextSelectedBg] = WithAlpha(accent, 0.35f);
    c[ImGuiCol_DragDropTarget] = accent;
    c[ImGuiCol_NavHighlight] = accent;
    c[ImGuiCol_NavWindowingHighlight] = Hex(0xFFFFFF, 0.70f);
    c[ImGuiCol_NavWindowingDimBg] = Hex(0x000000, 0.40f);
    c[ImGuiCol_ModalWindowDimBg] = Hex(0x000000, 0.50f);
    c[ImGuiCol_PlotLines] = accent;
    c[ImGuiCol_PlotLinesHovered] = Hex(0x9A8CF5);
    c[ImGuiCol_PlotHistogram] = accent;
    c[ImGuiCol_PlotHistogramHovered] = Hex(0x9A8CF5);

    // Keep DllStalker density: scale widget metrics after base Umbra geometry.
    style.ScaleAllSizes(Config::GUI_SCALE);
}

const ColorTokens& Tokens()
{
    if (!g_tokensReady) {
        InitTokens();
    }
    return g_tokens;
}

void PushUnderlineTabStyle()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f * Config::GUI_SCALE, 8.0f * Config::GUI_SCALE));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(8.0f * Config::GUI_SCALE, 4.0f * Config::GUI_SCALE));
}

void PopUnderlineTabStyle()
{
    ImGui::PopStyleVar(2);
}

bool BeginUnderlineTabBar(const char* id)
{
    PushUnderlineTabStyle();
    const bool open = ImGui::BeginTabBar(
        id, ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_DrawSelectedOverline);
    if (!open) {
        PopUnderlineTabStyle();
    }
    return open;
}

void EndUnderlineTabBar()
{
    ImGui::EndTabBar();
    PopUnderlineTabStyle();
}

bool PrimaryButton(const char* label, const ImVec2& size)
{
    const ColorTokens& t = Tokens();
    ImGui::PushStyleColor(ImGuiCol_Button, t.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(t.accent, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, WithAlpha(t.accent, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

bool CenteredGlyphButton(const char* id, const char* glyph, float width,
                         const ImVec4* text_color)
{
    if (glyph == nullptr) {
        glyph = "";
    }
    const float height = ImGui::GetFrameHeight();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();
    if (hovered || active) {
        const ImU32 bg = ImGui::ColorConvertFloat4ToU32(
            active ? WithAlpha(Tokens().accent, 0.35f) : WithAlpha(Tokens().accent, 0.20f));
        draw->AddRectFilled(rmin, rmax, bg, ImGui::GetStyle().FrameRounding);
    }

    const ImVec2 text_size = ImGui::CalcTextSize(glyph);
    const ImVec2 pos(rmin.x + (rmax.x - rmin.x - text_size.x) * 0.5f,
                     rmin.y + (rmax.y - rmin.y - text_size.y) * 0.5f);
    const ImU32 col = text_color != nullptr ? ImGui::ColorConvertFloat4ToU32(*text_color)
                                            : ImGui::GetColorU32(ImGuiCol_Text);
    draw->AddText(pos, col, glyph);
    return pressed;
}

void CenteredDisabledGlyph(const char* glyph, float width)
{
    if (glyph == nullptr) {
        glyph = "-";
    }
    const float height = ImGui::GetFrameHeight();
    ImGui::BeginDisabled();
    ImGui::InvisibleButton("##disabled_glyph", ImVec2(width, height));
    ImGui::EndDisabled();

    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();
    const ImVec2 text_size = ImGui::CalcTextSize(glyph);
    const ImVec2 pos(rmin.x + (rmax.x - rmin.x - text_size.x) * 0.5f,
                     rmin.y + (rmax.y - rmin.y - text_size.y) * 0.5f);
    ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), glyph);
}

bool BrowseSelectable(const char* display, bool selected, float indent)
{
    if (display == nullptr) {
        display = "";
    }
    if (indent > 0.0f) {
        ImGui::Indent(indent);
    }

    ImFont* font = ImGui::GetFont();
    const float font_size = ImGui::GetFontSize();
    const float text_h = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, "Ay").y;
    const float line_h = text_h + ImGui::GetStyle().ItemSpacing.y * 0.35f;
    float avail_w = ImGui::GetContentRegionAvail().x;
    if (avail_w < 1.0f) {
        avail_w = 1.0f;
    }
    // Positive width required on ImGui 1.92 — do not pass -FLT_MIN.
    const bool clicked = ImGui::Selectable("##browse_row", selected, ImGuiSelectableFlags_None,
                                           ImVec2(avail_w, line_h));

    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rsize = ImGui::GetItemRectSize();
    const float ty = rmin.y + (rsize.y - text_h) * 0.5f;
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    ImGui::GetWindowDrawList()->AddText(font, font_size, ImVec2(rmin.x, ty), col, display);

    if (indent > 0.0f) {
        ImGui::Unindent(indent);
    }
    return clicked;
}

bool ElevatedFilter(const char* id, char* buf, size_t buf_size, float width, const char* hint)
{
    if (width < 0.0f) {
        ImGui::SetNextItemWidth(-1.0f);
    }
    else {
        ImGui::SetNextItemWidth(width);
    }
    const ColorTokens& t = Tokens();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, t.bg3);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, WithAlpha(t.accent, 0.35f));
    const bool changed = (hint != nullptr && hint[0] != '\0')
        ? ImGui::InputTextWithHint(id, hint, buf, buf_size)
        : ImGui::InputText(id, buf, buf_size);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    return changed;
}

bool GhostButton(const char* label, const ImVec4* text_color)
{
    const ColorTokens& t = Tokens();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(t.accent, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, WithAlpha(t.accent, 0.35f));
    ImGui::PushStyleColor(ImGuiCol_Text, text_color != nullptr ? *text_color : t.text);
    const bool pressed = ImGui::Button(label);
    ImGui::PopStyleColor(4);
    return pressed;
}

bool ChipToggle(const char* label, bool* value)
{
    if (value == nullptr) {
        return false;
    }
    const ColorTokens& t = Tokens();
    const ImVec4 text = *value ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : t.text_disabled;
    const bool pressed = GhostButton(label, &text);
    if (pressed) {
        *value = !*value;
        return true;
    }
    return false;
}

ImGuiTableFlags InspectorTableFlags()
{
    return ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter |
           ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
           ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
}

bool BeginInspectorTable(const char* id, int columns)
{
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                        ImVec2(8.0f * Config::GUI_SCALE, 5.0f * Config::GUI_SCALE));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(8.0f * Config::GUI_SCALE, 3.0f * Config::GUI_SCALE));
    const bool open = ImGui::BeginTable(id, columns, InspectorTableFlags());
    if (!open) {
        ImGui::PopStyleVar(2);
    }
    return open;
}

void EndInspectorTable()
{
    ImGui::EndTable();
    ImGui::PopStyleVar(2);
}

void DrawColumnName(const char* text)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(Tokens().text, "%s", text != nullptr ? text : "");
}

void DrawColumnType(const char* text)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(Tokens().text_disabled, "%s", text != nullptr ? text : "");
}

void DrawColumnOffset(const char* text)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(Tokens().semantic_link, "%s", text != nullptr ? text : "");
}

void DrawColumnValue(const char* text)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(Tokens().text, "%s", text != nullptr ? text : "");
}

void DrawMutedFooter(const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(text != nullptr ? text : "");
    ImGui::PopStyleColor();
}

void DrawSuccessText(const char* text)
{
    ImGui::TextColored(Tokens().success, "%s", text != nullptr ? text : "");
}

void DrawWarningText(const char* text)
{
    ImGui::TextColored(Tokens().warning, "%s", text != nullptr ? text : "");
}

void DrawErrorText(const char* text)
{
    ImGui::TextColored(Tokens().error, "%s", text != nullptr ? text : "");
}
} // namespace Gui::UiTheme

#endif // ENABLE_DUMPER
