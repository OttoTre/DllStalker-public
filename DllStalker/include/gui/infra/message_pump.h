#pragma once

#include "pch.h"

#include <windows.h>
#include <chrono>

namespace Gui::Infra
{
bool IsUiInputMessage(UINT message);
bool ProcessControlPanelMessages(MSG& msg, bool& hadInputMessage);
bool WaitForRenderTriggerIfNeeded(bool& requestRender, bool hadInputMessage, DWORD idleWakeMs = INFINITE);
bool WaitForFramePacingIfNeeded(const std::chrono::steady_clock::time_point& nextFrameAt);
void UpdateFramePacing(bool hadInputMessage,
    bool& requestRender,
    std::chrono::steady_clock::time_point& nextFrameAt);
bool IsCrashHandler();
} // namespace Gui::Infra
