#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/chrome/ui_icons.h"
#include "gui/chrome/ui_theme.h"

#include "imgui.h"

#include <cmath>
#include <cstdint>

namespace Gui::UiTheme
{
namespace
{
ImVec4 WithAlpha(const ImVec4& c, float a)
{
    return ImVec4(c.x, c.y, c.z, a);
}

void DrawRefreshIcon(ImDrawList* draw, ImVec2 p0, float size, ImU32 col, float angle_offset = 0.0f)
{
    const float cx = p0.x + size * 0.50f;
    const float cy = p0.y + size * 0.50f;
    const float r = size * 0.28f;
    const float thick = 1.25f;
    const ImVec2 center(cx, cy);
    constexpr float kPi = 3.14159265f;
    const float a0 = -kPi * 0.65f + angle_offset;
    const float a1 = kPi * 1.05f + angle_offset;
    draw->PathClear();
    draw->PathArcTo(center, r, a0, a1, 16);
    draw->PathStroke(col, 0, thick);
    const float tip_x = cx + std::cos(a0) * r;
    const float tip_y = cy + std::sin(a0) * r;
    const float wing = size * 0.11f;
    const float tangent = a0 + kPi * 0.5f;
    const ImVec2 tip(tip_x, tip_y);
    const ImVec2 w0(tip_x + std::cos(tangent - 0.55f) * wing, tip_y + std::sin(tangent - 0.55f) * wing);
    const ImVec2 w1(tip_x + std::cos(tangent + 2.1f) * wing, tip_y + std::sin(tangent + 2.1f) * wing);
    draw->AddTriangleFilled(tip, w0, w1, col);
}

void DrawPlayIcon(ImDrawList* draw, ImVec2 p0, float size, ImU32 col)
{
    const float pad = size * 0.22f;
    const ImVec2 a(p0.x + pad, p0.y + pad);
    const ImVec2 b(p0.x + pad, p0.y + size - pad);
    const ImVec2 c(p0.x + size - pad, p0.y + size * 0.50f);
    draw->AddTriangleFilled(a, b, c, col);
}

void DrawStarIcon(ImDrawList* draw, ImVec2 p0, float size, ImU32 col, bool filled)
{
    constexpr float kPi = 3.14159265f;
    const float cx = p0.x + size * 0.50f;
    const float cy = p0.y + size * 0.50f;
    const float outer = size * 0.36f;
    const float inner = size * 0.15f;
    ImVec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        const float radius = (i % 2 == 0) ? outer : inner;
        const float angle = -kPi * 0.5f + static_cast<float>(i) * (kPi / 5.0f);
        pts[i] = ImVec2(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius);
    }
    if (filled) {
        const ImVec2 center(cx, cy);
        for (int i = 0; i < 10; ++i) {
            draw->AddTriangleFilled(center, pts[i], pts[(i + 1) % 10], col);
        }
    }
    else {
        draw->AddPolyline(pts, 10, col, ImDrawFlags_Closed, 1.25f);
    }
}

void DrawSearchIcon(ImDrawList* draw, ImVec2 p0, float size, ImU32 col)
{
    const float cx = p0.x + size * 0.42f;
    const float cy = p0.y + size * 0.42f;
    const float r = size * 0.22f;
    draw->AddCircle(ImVec2(cx, cy), r, col, 16, 1.25f);
    const float hx0 = cx + r * 0.70f;
    const float hy0 = cy + r * 0.70f;
    const float hx1 = p0.x + size * 0.78f;
    const float hy1 = p0.y + size * 0.78f;
    draw->AddLine(ImVec2(hx0, hy0), ImVec2(hx1, hy1), col, 1.25f);
}

// Umbra funnel / narrow glyph (Search Drill) — wide top → stem.
void DrawDrillIcon(ImDrawList* draw, ImVec2 p0, float size, ImU32 col)
{
    const float pad_x = size * 0.18f;
    const float pad_y = size * 0.18f;
    const float mid_y = p0.y + size * 0.52f;
    const float stem_w = size * 0.16f;
    const float cx = p0.x + size * 0.50f;
    const ImVec2 top_l(p0.x + pad_x, p0.y + pad_y);
    const ImVec2 top_r(p0.x + size - pad_x, p0.y + pad_y);
    const ImVec2 mid_l(cx - stem_w * 0.5f, mid_y);
    const ImVec2 mid_r(cx + stem_w * 0.5f, mid_y);
    draw->AddTriangleFilled(top_l, top_r, mid_r, col);
    draw->AddTriangleFilled(top_l, mid_r, mid_l, col);
    draw->AddRectFilled(ImVec2(cx - stem_w * 0.5f, mid_y),
                        ImVec2(cx + stem_w * 0.5f, p0.y + size - pad_y), col);
}

// Umbra-adjacent Search control language; Deep ≠ Drill funnel.
// Simple tree: one root node + two child nodes (not a busy binary tree).
void DrawDeepIcon(ImDrawList* draw, ImVec2 p0, float size, ImU32 col)
{
    const float thick = 1.25f;
    const float node_r = size * 0.085f;
    const ImVec2 root(p0.x + size * 0.50f, p0.y + size * 0.28f);
    const ImVec2 child_l(p0.x + size * 0.28f, p0.y + size * 0.72f);
    const ImVec2 child_r(p0.x + size * 0.72f, p0.y + size * 0.72f);
    draw->AddLine(root, child_l, col, thick);
    draw->AddLine(root, child_r, col, thick);
    draw->AddCircleFilled(root, node_r, col, 12);
    draw->AddCircleFilled(child_l, node_r, col, 12);
    draw->AddCircleFilled(child_r, node_r, col, 12);
}

void DrawStopIcon(ImDrawList* draw, ImVec2 p0, float size, ImU32 col)
{
    const float padX = size * 0.30f;
    const float padY = size * 0.26f;
    const float barW = size * 0.14f;
    const float gap = size * 0.12f;
    const float x0 = p0.x + padX;
    const float x1 = x0 + barW + gap;
    const float y0 = p0.y + padY;
    const float y1 = p0.y + size - padY;
    draw->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + barW, y1), col);
    draw->AddRectFilled(ImVec2(x1, y0), ImVec2(x1 + barW, y1), col);
}

void DrawSnapshotIcon(ImDrawList* draw, ImVec2 p0, float size, ImU32 col)
{
    const float thick = 1.25f;
    const float bodyL = p0.x + size * 0.18f;
    const float bodyR = p0.x + size * 0.82f;
    const float bodyT = p0.y + size * 0.36f;
    const float bodyB = p0.y + size * 0.82f;
    // Body
    draw->AddRect(ImVec2(bodyL, bodyT), ImVec2(bodyR, bodyB), col, 2.0f, 0, thick);

    const float bumpW = size * 0.22f;
    const float bumpH = size * 0.12f;
    const float bumpL = p0.x + size * 0.38f;
    // Viewfinder
    draw->AddRect(ImVec2(bumpL, bodyT - bumpH), ImVec2(bumpL + bumpW, bodyT), col, 1.0f, 0, thick);

    const float cx = (bodyL + bodyR) * 0.5f;
    const float cy = (bodyT + bodyB) * 0.5f;
    // Lens
    draw->AddCircle(ImVec2(cx, cy), size * 0.14f, col, 16, thick);
    draw->AddCircle(ImVec2(cx, cy), size * 0.05f, col, 12, thick);
}

void DrawLockIcon(ImDrawList* draw, ImVec2 p0, float size, ImU32 col, bool locked)
{
    const float thick = 1.25f;
    const float bodyL = p0.x + size * 0.28f;
    const float bodyR = p0.x + size * 0.72f;
    const float bodyT = p0.y + size * 0.50f;
    const float bodyB = p0.y + size * 0.82f;
    // Body
    draw->AddRect(ImVec2(bodyL, bodyT), ImVec2(bodyR, bodyB), col, 1.5f, 0, thick);

    const float cx = (bodyL + bodyR) * 0.5f;
    const float shackleR = size * 0.15f;
    // Shackle arc above body (Y-down: π→2π).
    const float shackleCy = bodyT - shackleR * 0.15f;
    constexpr float kPi = 3.14159265f;
    if (locked) {
        draw->PathClear();
        draw->PathArcTo(ImVec2(cx, shackleCy), shackleR, kPi, kPi * 2.0f, 12);
        draw->PathStroke(col, 0, thick);
        draw->AddLine(ImVec2(cx - shackleR, shackleCy), ImVec2(cx - shackleR, bodyT), col, thick);
        draw->AddLine(ImVec2(cx + shackleR, shackleCy), ImVec2(cx + shackleR, bodyT), col, thick);
    }
    else {
        // Open shackle: left leg + partial arc.
        draw->PathClear();
        draw->PathArcTo(ImVec2(cx, shackleCy), shackleR, kPi, kPi * 1.65f, 12);
        draw->PathStroke(col, 0, thick);
        draw->AddLine(ImVec2(cx - shackleR, shackleCy), ImVec2(cx - shackleR, bodyT), col, thick);
    }

    draw->AddCircleFilled(ImVec2(cx, bodyT + (bodyB - bodyT) * 0.38f), size * 0.045f, col);
    draw->AddLine(ImVec2(cx, bodyT + (bodyB - bodyT) * 0.42f),
                  ImVec2(cx, bodyT + (bodyB - bodyT) * 0.72f), col, thick);
}

void DrawPowerIcon(ImDrawList* draw, ImVec2 p0, float size, ImU32 col, bool active)
{
    const float thick = 1.35f;
    const float cx = p0.x + size * 0.50f;
    const float cy = p0.y + size * 0.54f;
    const float r = size * 0.28f;
    constexpr float kPi = 3.14159265f;
    // Arc with gap at top.
    const float a0 = -kPi * 0.5f + 0.55f;
    const float a1 = -kPi * 0.5f - 0.55f + kPi * 2.0f;
    draw->PathClear();
    draw->PathArcTo(ImVec2(cx, cy), r, a0, a1, 20);
    draw->PathStroke(col, 0, thick);

    // Stem
    const float stemTop = p0.y + size * 0.18f;
    const float stemBot = cy - r * 0.15f;
    draw->AddLine(ImVec2(cx, stemTop), ImVec2(cx, stemBot), col, thick);

    (void)active;
}

void DrawTrashIcon(ImDrawList* draw, ImVec2 p0, float size, ImU32 col)
{
    const float thick = 1.25f;
    const float lidY = p0.y + size * 0.28f;
    const float bodyT = p0.y + size * 0.38f;
    const float bodyB = p0.y + size * 0.82f;
    const float bodyL = p0.x + size * 0.30f;
    const float bodyR = p0.x + size * 0.70f;
    // Lid
    draw->AddLine(ImVec2(p0.x + size * 0.22f, lidY), ImVec2(p0.x + size * 0.78f, lidY), col, thick);
    // Handle bump
    draw->AddLine(ImVec2(p0.x + size * 0.40f, lidY), ImVec2(p0.x + size * 0.40f, lidY - size * 0.08f), col, thick);
    draw->AddLine(ImVec2(p0.x + size * 0.40f, lidY - size * 0.08f),
                  ImVec2(p0.x + size * 0.60f, lidY - size * 0.08f), col, thick);
    draw->AddLine(ImVec2(p0.x + size * 0.60f, lidY - size * 0.08f), ImVec2(p0.x + size * 0.60f, lidY), col, thick);
    // Can body
    draw->AddLine(ImVec2(bodyL, bodyT), ImVec2(bodyL + size * 0.04f, bodyB), col, thick);
    draw->AddLine(ImVec2(bodyR, bodyT), ImVec2(bodyR - size * 0.04f, bodyB), col, thick);
    draw->AddLine(ImVec2(bodyL + size * 0.04f, bodyB), ImVec2(bodyR - size * 0.04f, bodyB), col, thick);
    // Ribs
    const float mid1 = p0.x + size * 0.42f;
    const float mid2 = p0.x + size * 0.50f;
    const float mid3 = p0.x + size * 0.58f;
    draw->AddLine(ImVec2(mid1, bodyT + size * 0.06f), ImVec2(mid1, bodyB - size * 0.06f), col, thick);
    draw->AddLine(ImVec2(mid2, bodyT + size * 0.06f), ImVec2(mid2, bodyB - size * 0.06f), col, thick);
    draw->AddLine(ImVec2(mid3, bodyT + size * 0.06f), ImVec2(mid3, bodyB - size * 0.06f), col, thick);
}

float CompactIconSize()
{
    return ImGui::GetFrameHeight();
}

enum class IconColorMode : uint8_t
{
    Fixed,
    HoverBrighten,
    StateAccent,
};

using IconDrawFn = void (*)(ImDrawList* draw, ImVec2 p0, float size, ImU32 col, void* user);

struct IconButtonOpts
{
    const char* id = nullptr;
    float size = -1.0f;
    const char* tooltip = nullptr;
    IconDrawFn draw_icon = nullptr;
    void* draw_user = nullptr;
    IconColorMode color_mode = IconColorMode::Fixed;
    ImU32 accent_col = 0;
    bool state_on = false;
    bool enabled = true;
    bool suppress_hover_wash = false;
    bool accept_press = true;
};

bool IconButtonCore(const IconButtonOpts& opts)
{
    float size = opts.size;
    if (size < 0.0f) {
        size = CompactIconSize();
    }

    const bool pressed = ImGui::InvisibleButton(opts.id, ImVec2(size, size));
    const bool tip_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool hover_wash = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (opts.tooltip != nullptr && tip_hovered) {
        ImGui::SetTooltip("%s", opts.tooltip);
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 rmin = ImGui::GetItemRectMin();
    const ImVec2 rmax = ImGui::GetItemRectMax();

    const ColorTokens& tokens = Tokens();
    const bool show_wash = opts.enabled && !opts.suppress_hover_wash && hover_wash;
    if (show_wash) {
        const ImU32 bg = ImGui::ColorConvertFloat4ToU32(
            ImGui::IsItemActive() ? WithAlpha(tokens.accent, 0.35f)
                                  : WithAlpha(tokens.accent, 0.20f));
        draw->AddRectFilled(rmin, rmax, bg, ImGui::GetStyle().FrameRounding);
    }

    ImU32 col;
    if (!opts.enabled) {
        col = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    }
    else if (opts.color_mode == IconColorMode::Fixed) {
        col = opts.accent_col;
    }
    else if (opts.color_mode == IconColorMode::StateAccent && opts.state_on) {
        col = opts.accent_col;
    }
    else {
        col = ImGui::GetColorU32(hover_wash ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    }

    opts.draw_icon(draw, rmin, size, col, opts.draw_user);
    return opts.enabled && opts.accept_press && pressed;
}

void DrawPlayAdapter(ImDrawList* d, ImVec2 p, float s, ImU32 c, void*)
{
    DrawPlayIcon(d, p, s, c);
}
void DrawStopAdapter(ImDrawList* d, ImVec2 p, float s, ImU32 c, void*)
{
    DrawStopIcon(d, p, s, c);
}
void DrawTrashAdapter(ImDrawList* d, ImVec2 p, float s, ImU32 c, void*)
{
    DrawTrashIcon(d, p, s, c);
}
void DrawSearchAdapter(ImDrawList* d, ImVec2 p, float s, ImU32 c, void*)
{
    DrawSearchIcon(d, p, s, c);
}
void DrawDrillAdapter(ImDrawList* d, ImVec2 p, float s, ImU32 c, void*)
{
    DrawDrillIcon(d, p, s, c);
}
void DrawDeepAdapter(ImDrawList* d, ImVec2 p, float s, ImU32 c, void*)
{
    DrawDeepIcon(d, p, s, c);
}
void DrawSnapshotAdapter(ImDrawList* d, ImVec2 p, float s, ImU32 c, void*)
{
    DrawSnapshotIcon(d, p, s, c);
}
void DrawRefreshAdapter(ImDrawList* d, ImVec2 p, float s, ImU32 c, void* user)
{
    const float spin = user != nullptr ? *static_cast<const float*>(user) : 0.0f;
    DrawRefreshIcon(d, p, s, c, spin);
}
void DrawStarAdapter(ImDrawList* d, ImVec2 p, float s, ImU32 c, void* user)
{
    const bool filled = user != nullptr && *static_cast<const bool*>(user);
    DrawStarIcon(d, p, s, c, filled);
}
void DrawLockAdapter(ImDrawList* d, ImVec2 p, float s, ImU32 c, void* user)
{
    const bool locked = user != nullptr && *static_cast<const bool*>(user);
    DrawLockIcon(d, p, s, c, locked);
}
void DrawPowerAdapter(ImDrawList* d, ImVec2 p, float s, ImU32 c, void* user)
{
    const bool active = user != nullptr && *static_cast<const bool*>(user);
    DrawPowerIcon(d, p, s, c, active);
}
} // namespace

bool IconPlayButton(const char* id, const char* tooltip, float size, bool enabled)
{
    IconButtonOpts opts{};
    opts.id = id;
    opts.size = size;
    opts.tooltip = tooltip;
    opts.draw_icon = DrawPlayAdapter;
    opts.color_mode = IconColorMode::Fixed;
    opts.accent_col = ImGui::ColorConvertFloat4ToU32(Tokens().success);
    opts.enabled = enabled;
    return IconButtonCore(opts);
}

bool IconRefreshButton(const char* id, const char* tooltip, float size, float spin_radians)
{
    const bool spinning = spin_radians != 0.0f;
    IconButtonOpts opts{};
    opts.id = id;
    opts.size = size;
    opts.tooltip = tooltip;
    opts.draw_icon = DrawRefreshAdapter;
    opts.draw_user = &spin_radians;
    if (spinning) {
        opts.color_mode = IconColorMode::Fixed;
        opts.accent_col = ImGui::GetColorU32(ImGuiCol_Text);
        opts.suppress_hover_wash = true;
        opts.accept_press = false;
        return IconButtonCore(opts);
    }
    opts.color_mode = IconColorMode::HoverBrighten;
    return IconButtonCore(opts);
}

bool IconStopButton(const char* id, const char* tooltip, float size, bool enabled)
{
    IconButtonOpts opts{};
    opts.id = id;
    opts.size = size;
    opts.tooltip = tooltip;
    opts.draw_icon = DrawStopAdapter;
    opts.color_mode = IconColorMode::Fixed;
    opts.accent_col = ImGui::ColorConvertFloat4ToU32(Tokens().error);
    opts.enabled = enabled;
    return IconButtonCore(opts);
}

bool IconTrashButton(const char* id, const char* tooltip, float size, bool enabled)
{
    IconButtonOpts opts{};
    opts.id = id;
    opts.size = size;
    opts.tooltip = tooltip;
    opts.draw_icon = DrawTrashAdapter;
    opts.color_mode = IconColorMode::Fixed;
    opts.accent_col = ImGui::GetColorU32(ImGuiCol_Text);
    opts.enabled = enabled;
    return IconButtonCore(opts);
}

bool IconStarButton(const char* id, bool filled, const char* tooltip)
{
    IconButtonOpts opts{};
    opts.id = id;
    opts.size = CompactIconSize();
    opts.tooltip = tooltip;
    opts.draw_icon = DrawStarAdapter;
    opts.draw_user = &filled;
    opts.color_mode = IconColorMode::StateAccent;
    opts.accent_col = ImGui::ColorConvertFloat4ToU32(Tokens().bookmark_gold);
    opts.state_on = filled;
    return IconButtonCore(opts);
}

bool IconSearchButton(const char* id, const char* tooltip, float size, bool enabled)
{
    // Full text colour when actionable (same CTA weight as Snapshot/camera).
    IconButtonOpts opts{};
    opts.id = id;
    opts.size = size;
    opts.tooltip = tooltip;
    opts.draw_icon = DrawSearchAdapter;
    opts.color_mode = IconColorMode::Fixed;
    opts.accent_col = ImGui::GetColorU32(ImGuiCol_Text);
    opts.enabled = enabled;
    return IconButtonCore(opts);
}

bool IconDrillButton(const char* id, const char* tooltip, float size, bool enabled)
{
    // Umbra: accent when Drill is available; muted when not.
    IconButtonOpts opts{};
    opts.id = id;
    opts.size = size;
    opts.tooltip = tooltip;
    opts.draw_icon = DrawDrillAdapter;
    opts.color_mode = IconColorMode::Fixed;
    opts.accent_col = ImGui::ColorConvertFloat4ToU32(Tokens().accent);
    opts.enabled = enabled;
    return IconButtonCore(opts);
}

bool IconDeepButton(const char* id, bool deep_on, const char* tooltip, float size)
{
    IconButtonOpts opts{};
    opts.id = id;
    opts.size = size;
    opts.tooltip = tooltip;
    opts.draw_icon = DrawDeepAdapter;
    opts.color_mode = IconColorMode::StateAccent;
    opts.accent_col = ImGui::ColorConvertFloat4ToU32(Tokens().accent);
    opts.state_on = deep_on;
    return IconButtonCore(opts);
}

bool IconLockButton(const char* id, bool locked, const char* tooltip)
{
    IconButtonOpts opts{};
    opts.id = id;
    opts.size = CompactIconSize();
    opts.tooltip = tooltip;
    opts.draw_icon = DrawLockAdapter;
    opts.draw_user = &locked;
    opts.color_mode = IconColorMode::StateAccent;
    opts.accent_col = ImGui::ColorConvertFloat4ToU32(Tokens().warning);
    opts.state_on = locked;
    return IconButtonCore(opts);
}

bool IconActiveButton(const char* id, bool is_active, const char* tooltip)
{
    IconButtonOpts opts{};
    opts.id = id;
    opts.size = CompactIconSize();
    opts.tooltip = tooltip;
    opts.draw_icon = DrawPowerAdapter;
    opts.draw_user = &is_active;
    opts.color_mode = IconColorMode::StateAccent;
    opts.accent_col = ImGui::ColorConvertFloat4ToU32(Tokens().success);
    opts.state_on = is_active;
    return IconButtonCore(opts);
}

bool IconSnapshotButton(const char* id, const char* tooltip, float size, bool enabled)
{
    IconButtonOpts opts{};
    opts.id = id;
    opts.size = size;
    opts.tooltip = tooltip;
    opts.draw_icon = DrawSnapshotAdapter;
    opts.color_mode = IconColorMode::Fixed;
    opts.accent_col = ImGui::GetColorU32(ImGuiCol_Text);
    opts.enabled = enabled;
    return IconButtonCore(opts);
}
} // namespace Gui::UiTheme

#endif // ENABLE_DUMPER
