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
struct TransformSource;
} // namespace Gui::State

namespace Gui::Views
{
void RenderTransformEditorPanel(ControlPanelSessionState& state,
                                Gui::State::TransformModel& model,
                                const Gui::State::TransformSource& selected);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
