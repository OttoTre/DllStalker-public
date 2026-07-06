#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include "gui/views/copy_feedback_state.h"

namespace Gui
{
struct ControlPanelSessionState;
struct InspectorCache;
}

namespace Gui::Views
{
void RenderMethodsTab(const InspectorCache& inspectorSnapshot,
                      CopyFeedbackState& copyFeedback,
                      bool inspectorLoadInProgress,
                      ControlPanelSessionState& state);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
