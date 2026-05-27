#pragma once

#include "pch.h"

#include "gui/app/app_shell.h"
#include "gui/session_state.h"

namespace Gui::Views
{
void RenderInspector(ControlPanelSessionState& state, AppShell::CopyFeedbackState& copyFeedback);
} // namespace Gui::Views
