#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/dock/call_logger_tab.h"

#include "gui/chrome/ui_theme.h"
#include "gui/session_state.h"
#include "gui/config.h"

#include "imgui.h"

#include <cstdio>
#include <string>
#include <vector>

namespace Gui::Views
{
namespace
{
void RenderActiveHooksTable(ControlPanelSessionState& state) {
    std::vector<Engine::Services::CallLogHookSpec> hooks;
    {
        std::lock_guard<std::mutex> lock(state.callLog.mutex);
        hooks = state.callLog.hooks;
    }

    if (hooks.empty()) {
        ImGui::TextDisabled("No active hooks. Toggle Log on a method in the Methods tab.");
        return;
    }

    const float tableHeight = ImGui::GetContentRegionAvail().y;
    if (!ImGui::BeginChild("CallLogHooksScroll", ImVec2(0, tableHeight), false)) {
        ImGui::EndChild();
        return;
    }

    if (ImGui::BeginTable("CallLogHooksTable",
                          2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
                              | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Method", ImGuiTableColumnFlags_WidthStretch, 0.75f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableHeadersRow();

        for (const auto& hook : hooks) {
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(hook.hookId));

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(hook.DisplayLabel().c_str());

            ImGui::TableSetColumnIndex(1);
            if (ImGui::SmallButton("Remove##remove")) {
                state.callLog.RemoveHook(hook.hookId);
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
}

void RenderCallLogLines(ControlPanelSessionState& state) {
    const std::vector<std::string> lines = state.callLog.SnapshotLines();

    const float logHeight = ImGui::GetContentRegionAvail().y;
    if (!ImGui::BeginChild("CallLogScroll", ImVec2(0, logHeight), true, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return;
    }

    if (lines.empty()) {
        ImGui::TextUnformatted("Log is empty.");
    }
    else {
        for (const auto& line : lines) {
            ImGui::TextUnformatted(line.c_str());
        }

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }

    ImGui::EndChild();
}
} // namespace

void RenderCallLoggerTab(ControlPanelSessionState& state) {
    state.callLog.EnsureLineCallbackRegistered();

    size_t hookCount = 0;
    size_t lineCount = 0;
    {
        std::lock_guard<std::mutex> lock(state.callLog.mutex);
        hookCount = state.callLog.hooks.size();
        lineCount = state.callLog.lines.size();
    }

    if (!UiTheme::BeginUnderlineTabBar("LoggerInner")) {
        return;
    }

    char hooksTabLabel[48];
    if (hookCount > 0) {
        snprintf(hooksTabLabel, sizeof(hooksTabLabel), "Hooks (%zu)###LoggerHooks", hookCount);
    }
    else {
        snprintf(hooksTabLabel, sizeof(hooksTabLabel), "Hooks###LoggerHooks");
    }

    if (ImGui::BeginTabItem(hooksTabLabel)) {
        ImGui::TextDisabled("%zu / %zu hooks", hookCount, Gui::State::CallLogModel::kMaxHooks);
        RenderActiveHooksTable(state);
        ImGui::EndTabItem();
    }

    char logTabLabel[48];
    if (lineCount > 0) {
        snprintf(logTabLabel, sizeof(logTabLabel), "Log (%zu)###LoggerLog", lineCount);
    }
    else {
        snprintf(logTabLabel, sizeof(logTabLabel), "Log###LoggerLog");
    }

    if (ImGui::BeginTabItem(logTabLabel)) {
        if (ImGui::Button("Clear log##logger")) {
            state.callLog.ClearLines();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%zu / %zu lines", lineCount, Gui::State::CallLogModel::kMaxLines);
        RenderCallLogLines(state);
        ImGui::EndTabItem();
    }

    UiTheme::EndUnderlineTabBar();
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
