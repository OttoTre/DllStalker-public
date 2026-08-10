#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include "gui/views/copy_feedback_state.h"

namespace Gui
{
struct ControlPanelSessionState;
}

namespace Gui::Views
{
void RenderInspector(ControlPanelSessionState& state, CopyFeedbackState& copyFeedback);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
