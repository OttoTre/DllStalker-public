#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

namespace Gui
{
struct ControlPanelSessionState;
}

namespace Gui::Views
{
void RenderBreadcrumbBar(ControlPanelSessionState& state);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
