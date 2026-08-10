#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/window.h"

#include "services/application_quit_hook.h"

#include "imgui.h"
#include "imgui_impl_win32.h"

#include <atomic>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Gui::Window
{
namespace
{
ResizeCallback g_resizeCallback = nullptr;
std::atomic<HWND> g_controlPanelHwnd{nullptr};
std::atomic<bool> g_hostQuitting{false};

// Unity player HWND (UnityWndClass preferred) for IsWindow poll on soft Quit.
HWND g_latchedHostHwnd = nullptr;

struct FindHostWindowCtx {
    DWORD pid = 0;
    HWND exclude = nullptr;
    HWND unityWnd = nullptr;
    HWND fallback = nullptr;
};

BOOL CALLBACK EnumHostWindowProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<FindHostWindowCtx*>(lParam);
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid != ctx->pid || hwnd == ctx->exclude) {
        return TRUE;
    }
    if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }

    wchar_t className[64] = {};
    if (GetClassNameW(hwnd, className, 64) > 0
        && wcscmp(className, L"UnityWndClass") == 0) {
        ctx->unityWnd = hwnd;
        return FALSE; // Prefer UnityWndClass; stop scan.
    }
    if (!ctx->fallback) {
        ctx->fallback = hwnd;
    }
    return TRUE;
}

HWND FindHostGameWindow(HWND excludePanel) {
    FindHostWindowCtx ctx{};
    ctx.pid = GetCurrentProcessId();
    ctx.exclude = excludePanel;
    EnumWindows(EnumHostWindowProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.unityWnd ? ctx.unityWnd : ctx.fallback;
}

// Services detour notify: same end state as PollHostQuit (no gui/ in services).
void OnApplicationQuitNotify() {
    if (IsHostQuitting()) {
        return;
    }
    RequestHostQuit();
    const HWND panel = GetControlPanelHwnd();
    if (panel) {
        PostMessageW(panel, WM_CLOSE, 0, 0);
    }
}

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
        // Drop published HWND before the handle becomes invalid for PostMessage.
        Engine::Services::ApplicationQuitHook::SetQuitNotify(nullptr);
        g_controlPanelHwnd.store(nullptr, std::memory_order_release);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
} // namespace

bool Create(ControlPanelWindow& window, const wchar_t* className, const wchar_t* title, ResizeCallback onResize, int width, int height) {
    g_resizeCallback = onResize;

    window.windowClass = { sizeof(window.windowClass), CS_CLASSDC, WindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, className, NULL };
    RegisterClassExW(&window.windowClass);

    window.hwnd = CreateWindowW(window.windowClass.lpszClassName, title, WS_OVERLAPPEDWINDOW, 100, 100, width, height, NULL, NULL, window.windowClass.hInstance, NULL);
    if (window.hwnd) {
        g_controlPanelHwnd.store(window.hwnd, std::memory_order_release);
        Engine::Services::ApplicationQuitHook::SetQuitNotify(&OnApplicationQuitNotify);
    }
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
    // Unregister before DestroyWindow so a Quit detour cannot PostMessage a dying HWND.
    Engine::Services::ApplicationQuitHook::SetQuitNotify(nullptr);

    if (window.hwnd) {
        DestroyWindow(window.hwnd);
        window.hwnd = nullptr;
    }

    // Ensure no stale HWND before UnregisterClass (WM_DESTROY already cleared on normal path).
    g_controlPanelHwnd.store(nullptr, std::memory_order_release);

    if (window.windowClass.lpszClassName != nullptr) {
        UnregisterClassW(window.windowClass.lpszClassName, window.windowClass.hInstance);
    }

    g_resizeCallback = nullptr;
}

HWND GetControlPanelHwnd() {
    return g_controlPanelHwnd.load(std::memory_order_acquire);
}

void RequestHostQuit() {
    g_hostQuitting.store(true, std::memory_order_release);
}

bool IsHostQuitting() {
    return g_hostQuitting.load(std::memory_order_acquire);
}

void PollHostQuit() {
    // Idempotent: only one quit request + one WM_CLOSE post.
    if (IsHostQuitting()) {
        return;
    }

    const HWND panel = GetControlPanelHwnd();
    if (!g_latchedHostHwnd) {
        g_latchedHostHwnd = FindHostGameWindow(panel);
        return;
    }

    if (IsWindow(g_latchedHostHwnd)) {
        return;
    }

    RequestHostQuit();
    if (panel) {
        // Contract: PostMessage WM_CLOSE from this thread; do not DestroyWindow
        // cross-thread (panel HWND belongs to this GUI thread anyway).
        PostMessageW(panel, WM_CLOSE, 0, 0);
    }
}
} // namespace Gui::Window

#endif // ENABLE_DUMPER
