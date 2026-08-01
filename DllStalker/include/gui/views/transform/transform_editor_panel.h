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
} // namespace Gui::State

namespace Gui::Views
{
void RenderTransformEditorPanel(ControlPanelSessionState& state,
                                Gui::State::TransformModel& model);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
