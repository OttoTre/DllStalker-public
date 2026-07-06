#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

namespace Gui
{
struct ControlPanelSessionState;
}

namespace Gui::State
{
struct TransformModel;
}

namespace Gui::Views
{
void RenderTransformSourcesPanel(Gui::State::TransformModel& model);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
