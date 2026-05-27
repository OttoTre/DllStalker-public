#pragma once

#include "pch.h"

#include "gui/app/app_shell.h"
#include "gui/session_state.h"

namespace Gui::Views
{
void RenderFieldsTab(const InspectorCache& inspectorSnapshot,
                     AppShell::CopyFeedbackState& copyFeedback,
                     bool inspectorLoadInProgress,
                     ControlPanelSessionState& state);
} // namespace Gui::Views
