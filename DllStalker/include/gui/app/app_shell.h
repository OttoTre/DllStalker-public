#pragma once

#include "pch.h"

#include <windows.h>

#include "gui/views/copy_feedback_state.h"

namespace Gui
{
struct ControlPanelSessionState;
}

namespace Gui::AppShell
{
void BeginControlPanelFrame(HWND hwnd);
void RenderControlPanelContent(ControlPanelSessionState& state);

void RenderMainLayout(ControlPanelSessionState& state, Views::CopyFeedbackState& copyFeedback);
void RenderDumperInitialization(ControlPanelSessionState& state);
} // namespace Gui::AppShell
