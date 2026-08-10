#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

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

// Thread-safe live panel HWND for cross-thread PostMessage (e.g. host-quit → WM_CLOSE).
// Non-null only while the control-panel window exists; cleared on WM_DESTROY / Destroy.
HWND GetControlPanelHwnd();

// Host-quit intent flag (setters/getters only; BeginShutdown lives on session state).
void RequestHostQuit();
bool IsHostQuitting();

// GUI-loop tick: latch host HWND, then on !IsWindow → RequestHostQuit + PostMessage(WM_CLOSE).
// Idempotent; safe from the control-panel thread only (does not block DllMain).
void PollHostQuit();
} // namespace Gui::Window

#endif // ENABLE_DUMPER
