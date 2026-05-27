#pragma once

#include "pch.h"

#include <windows.h>

namespace Gui::Window
{
using ResizeCallback = void(*)(UINT width, UINT height);

struct ControlPanelWindow {
    WNDCLASSEXW windowClass{};
    HWND hwnd = nullptr;
};

bool Create(ControlPanelWindow& window, const wchar_t* className, const wchar_t* title, ResizeCallback onResize, int width = 1024, int height = 768);
void Show(const ControlPanelWindow& window);
void Destroy(ControlPanelWindow& window);
} // namespace Gui::Window
