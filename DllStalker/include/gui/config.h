#pragma once

namespace Gui::Config
{
constexpr float GUI_SCALE = 1.27f;

constexpr int BASE_WINDOW_WIDTH = 1024;
constexpr int BASE_WINDOW_HEIGHT = 768;

constexpr int DEFAULT_WINDOW_WIDTH = static_cast<int>(BASE_WINDOW_WIDTH * GUI_SCALE);
constexpr int DEFAULT_WINDOW_HEIGHT = static_cast<int>(BASE_WINDOW_HEIGHT * GUI_SCALE);
} // namespace Gui::Config
