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
void RenderFieldAnalysisToolbar(ControlPanelSessionState& state,
                                const InspectorCache& inspectorSnapshot,
                                bool inCollectionView,
                                bool fieldsBusy);
} // namespace Gui::Views

#endif // ENABLE_DUMPER
