#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/window.h"

#include "imgui.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Gui::Window
{
namespace
{
ResizeCallback g_resizeCallback = nullptr;

LRESULT WINAPI WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED && g_resizeCallback) {
            g_resizeCallback((UINT)LOWORD(lParam), (UINT)HIWORD(lParam));
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}
} // namespace

bool Create(ControlPanelWindow& window, const wchar_t* className, const wchar_t* title, ResizeCallback onResize, int width, int height) {
    g_resizeCallback = onResize;

    window.windowClass = { sizeof(window.windowClass), CS_CLASSDC, WindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, className, NULL };
    RegisterClassExW(&window.windowClass);

    window.hwnd = CreateWindowW(window.windowClass.lpszClassName, title, WS_OVERLAPPEDWINDOW, 100, 100, width, height, NULL, NULL, window.windowClass.hInstance, NULL);
    return window.hwnd != nullptr;
}

void Show(const ControlPanelWindow& window) {
    if (!window.hwnd) {
        return;
    }

    ShowWindow(window.hwnd, SW_SHOWDEFAULT);
    UpdateWindow(window.hwnd);
}

void Destroy(ControlPanelWindow& window) {
    if (window.hwnd) {
        DestroyWindow(window.hwnd);
        window.hwnd = nullptr;
    }

    if (window.windowClass.lpszClassName != nullptr) {
        UnregisterClassW(window.windowClass.lpszClassName, window.windowClass.hInstance);
    }

    g_resizeCallback = nullptr;
}
} // namespace Gui::Window

#endif // ENABLE_DUMPER
