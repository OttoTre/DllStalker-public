#pragma once

#include "pch.h"

#ifdef ENABLE_DUMPER

namespace Gui
{
struct ControlPanelSessionState;
} // namespace Gui

namespace Gui::Views
{
void RenderScriptingTab(ControlPanelSessionState& state);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
