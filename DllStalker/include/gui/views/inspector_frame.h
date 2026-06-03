#pragma once

#include "pch.h"

#include "gui/views/copy_feedback_state.h"

namespace Gui
{
struct ControlPanelSessionState;
}

namespace Gui::Views
{
void RenderInspector(ControlPanelSessionState& state, CopyFeedbackState& copyFeedback);
} // namespace Gui::Views
