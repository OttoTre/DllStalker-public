#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

namespace Gui
{
struct ControlPanelSessionState;
struct InspectorCache;
}

namespace Gui::Views
{
void RenderImageSelection(ControlPanelSessionState& state);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
