#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include "imgui.h"

namespace Gui::UiTheme
{
bool IconPlayButton(const char* id, const char* tooltip, float size = -1.0f, bool enabled = true);
bool IconRefreshButton(const char* id, const char* tooltip, float size = -1.0f,
                       float spin_radians = 0.0f);
bool IconStopButton(const char* id, const char* tooltip, float size = -1.0f, bool enabled = true);
bool IconTrashButton(const char* id, const char* tooltip, float size = -1.0f, bool enabled = true);
bool IconStarButton(const char* id, bool filled, const char* tooltip);
bool IconSearchButton(const char* id, const char* tooltip, float size = -1.0f);
bool IconLockButton(const char* id, bool locked, const char* tooltip);
bool IconActiveButton(const char* id, bool active, const char* tooltip);
bool IconSnapshotButton(const char* id, const char* tooltip, float size = -1.0f,
                        bool enabled = true);
} // namespace Gui::UiTheme

#endif // ENABLE_DUMPER
