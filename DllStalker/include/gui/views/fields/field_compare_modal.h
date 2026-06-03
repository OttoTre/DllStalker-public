#pragma once

#include "pch.h"

namespace Gui
{
struct ControlPanelSessionState;
struct InspectorCache;
}

namespace Gui::Views
{
void RequestOpenTwoInstanceCompareModal();
void RenderTwoInstanceCompareModal(ControlPanelSessionState& state,
                                   const InspectorCache& inspectorSnapshot);
} // namespace Gui::Views
