#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

namespace Gui
{
struct ControlPanelSessionState;
}

namespace Gui::Views
{
// continueSameLine: keep the first crumb on the current line (after bookmark star).
void RenderBreadcrumbBar(ControlPanelSessionState& state, bool continueSameLine = false);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
