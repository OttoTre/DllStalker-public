#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <windows.h>

namespace Gui
{
void CreateControlPanel();
DWORD WINAPI CreateControlPanelThread(LPVOID lpParam);
} // namespace Gui

#endif // ENABLE_DUMPER
