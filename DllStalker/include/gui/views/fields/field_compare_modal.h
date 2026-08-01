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
void RenderTwoInstanceComparePanel(ControlPanelSessionState& state,
                                   const InspectorCache& inspectorSnapshot);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
