#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/dock/utilities_dock.h"

#include "gui/session_state.h"
#include "gui/views/dock/bookmarks_tab.h"
#include "gui/views/dock/history_tab.h"
#include "gui/views/dock/watcher_tab.h"
#include "gui/views/dock/call_logger_tab.h"
#include "gui/views/dock/exporter_tab.h"
#include "gui/views/dock/scripting_tab.h"

#include "imgui.h"

namespace Gui::Views
{
void RenderUtilitiesDock(ControlPanelSessionState& state) {
    if (!ImGui::BeginTabBar("UtilitiesDock")) {
        return;
    }

    if (ImGui::BeginTabItem("Bookmarks")) {
        RenderBookmarksTab(state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("History")) {
        RenderHistoryTab(state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Watcher")) {
        RenderWatcherTab(state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Logger")) {
        RenderCallLoggerTab(state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Exporter")) {
        RenderExporterTab(state);
        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Scripting")) {
        RenderScriptingTab(state);
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
