#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/infra/message_pump.h"

#include <chrono>
#include <algorithm>

namespace Gui::Infra
{
bool IsUiInputMessage(UINT message) {
    switch (message) {
    case WM_MOUSEMOVE:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
    case WM_SIZE:
    case WM_PAINT:
        return true;
    default:
        return false;
    }
}

bool ProcessControlPanelMessages(MSG& msg, bool& hadInputMessage) {
    hadInputMessage = false;

    while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
        if (IsUiInputMessage(msg.message)) {
            hadInputMessage = true;
        }

        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (msg.message == WM_QUIT) {
            return false;
        }
    }

    return true;
}

bool WaitForRenderTriggerIfNeeded(bool& requestRender, bool hadInputMessage, DWORD idleWakeMs) {
    requestRender = requestRender || hadInputMessage;

    if (requestRender) {
        return false;
    }

    // Idle: wait for a message or timeout (Auto refresh / periodic ticks need the timeout).
    const DWORD result = MsgWaitForMultipleObjectsEx(0, nullptr, idleWakeMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (result == WAIT_TIMEOUT) {
        // Idle tick: drop through to render so periodic UI logic can run.
        requestRender = true;
        return false;
    }

    return true;
}

bool WaitForFramePacingIfNeeded(const std::chrono::steady_clock::time_point& nextFrameAt) {
    const auto now = std::chrono::steady_clock::now();
    if (now < nextFrameAt) {
        Sleep(1);
        return true;
    }

    return false;
}

void UpdateFramePacing(bool hadInputMessage, bool& requestRender, std::chrono::steady_clock::time_point& nextFrameAt) {
    requestRender = false;
    const auto frameEnd = std::chrono::steady_clock::now();

    // Input: ~kInputFrameBudgetMs floor. Idle / periodic: ~kIdleFrameBudgetMs.
    if (hadInputMessage) {
        nextFrameAt = frameEnd + std::chrono::milliseconds(kInputFrameBudgetMs);
    }
    else {
        nextFrameAt = frameEnd + std::chrono::milliseconds(kIdleFrameBudgetMs);
    }
}

bool IsCrashHandler() {
/* Avoid running second instance of GUI on UnityCrashHandler thread */
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring wsPath(path);
    std::transform(wsPath.begin(), wsPath.end(), wsPath.begin(), ::towlower);

    if (wsPath.find(L"unitycrashhandler") != std::wstring::npos) {
        return true;
    }
    return false;
}
} // namespace Gui::Infra

#endif // ENABLE_DUMPER
