#pragma once

#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/session_state.h"

namespace Gui::Views
{
void RenderTransformTab(ControlPanelSessionState& state, const InspectorCache& snapshot);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
