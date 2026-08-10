#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

struct ControlPanelSessionState;

namespace Gui::Views
{
void RenderSearchBrowser(ControlPanelSessionState& state);
void RenderSidebarBrowser(ControlPanelSessionState& state);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
