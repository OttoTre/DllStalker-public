#pragma once

#ifdef ENABLE_DUMPER

namespace Gui
{
struct ControlPanelSessionState;
} // namespace Gui

namespace Gui::Views
{
void RenderExporterTab(ControlPanelSessionState& state);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
