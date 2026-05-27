#pragma once

#include "pch.h"

#include <windows.h>

#include "gui/session_state.h"

namespace Gui::AppShell
{
struct CopyFeedbackState {
    char copiedMethodAddress[32] = {};
    float copiedAtSeconds = -1000.0f;
    char copiedFieldOffset[32] = {};
    float copiedFieldAtSeconds = -1000.0f;
};

void BeginControlPanelFrame(HWND hwnd);
void RenderControlPanelContent(ControlPanelSessionState& state);

void RenderMainLayout(ControlPanelSessionState& state, CopyFeedbackState& copyFeedback);
void RenderDumperInitialization(ControlPanelSessionState& state);
} // namespace Gui::AppShell
