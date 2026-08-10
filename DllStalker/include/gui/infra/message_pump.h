#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <windows.h>
#include <chrono>

namespace Gui::Infra
{
// Frame budgets for the control-panel pump (see UpdateFramePacing / idle clamp).
constexpr int kInputFrameBudgetMs = 8;
constexpr int kIdleFrameBudgetMs  = 100;

bool IsUiInputMessage(UINT message);
bool ProcessControlPanelMessages(MSG& msg, bool& hadInputMessage);
bool WaitForRenderTriggerIfNeeded(bool& requestRender, bool hadInputMessage, DWORD idleWakeMs = INFINITE);
bool WaitForFramePacingIfNeeded(const std::chrono::steady_clock::time_point& nextFrameAt);
void UpdateFramePacing(bool hadInputMessage,
    bool& requestRender,
    std::chrono::steady_clock::time_point& nextFrameAt);
bool IsCrashHandler();
} // namespace Gui::Infra

#endif // ENABLE_DUMPER
