#pragma once

#include "pch.h"

namespace Gui
{
struct ControlPanelSessionState;
struct InspectorCache;
}

namespace Gui::Views
{
void RenderFieldAnalysisToolbar(ControlPanelSessionState& state,
                                const InspectorCache& inspectorSnapshot,
                                bool inCollectionView,
                                bool fieldsBusy);
} // namespace Gui::Views
