#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/views/dock/scripting_tab.h"

#include "gui/session_state.h"
#include "gui/state/runtime/script_model.h"

#include "imgui.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace Gui::Views
{
namespace
{
const char* RunStateToText(Scripting::ScriptRunState state) {
    switch (state) {
    case Scripting::ScriptRunState::Idle: return "Idle";
    case Scripting::ScriptRunState::Running: return "Running";
    case Scripting::ScriptRunState::Stopping: return "Stopping";
    case Scripting::ScriptRunState::Reloading: return "Reloading";
    case Scripting::ScriptRunState::Completed: return "Completed";
    case Scripting::ScriptRunState::Cancelled: return "Cancelled";
    case Scripting::ScriptRunState::Failed: return "Failed";
    case Scripting::ScriptRunState::LoadError: return "Load error";
    case Scripting::ScriptRunState::Unresponsive: return "Unresponsive";
    case Scripting::ScriptRunState::Quarantined: return "Quarantined";
    default: return "Unknown";
    }
}

const char* StopReasonToText(Scripting::ScriptStopReason reason) {
    switch (reason) {
    case Scripting::ScriptStopReason::None: return "None";
    case Scripting::ScriptStopReason::UserRequest: return "User request";
    case Scripting::ScriptStopReason::Shutdown: return "Shutdown";
    default: return "Unknown";
    }
}

constexpr ImVec4 kWarningColor{ 0.95f, 0.85f, 0.25f, 1.0f };
constexpr ImVec4 kErrorColor{ 0.95f, 0.35f, 0.35f, 1.0f };

bool IsFailedRunState(Scripting::ScriptRunState state) {
    return state == Scripting::ScriptRunState::Failed
        || state == Scripting::ScriptRunState::LoadError
        || state == Scripting::ScriptRunState::Quarantined;
}

bool IsWarningRunState(Scripting::ScriptRunState state) {
    return state == Scripting::ScriptRunState::Cancelled
        || state == Scripting::ScriptRunState::Stopping
        || state == Scripting::ScriptRunState::Reloading
        || state == Scripting::ScriptRunState::Unresponsive;
}

bool IsActiveRunState(Scripting::ScriptRunState state) {
    return state == Scripting::ScriptRunState::Running
        || state == Scripting::ScriptRunState::Stopping
        || state == Scripting::ScriptRunState::Reloading
        || state == Scripting::ScriptRunState::Unresponsive;
}

bool IsPackageReadyStatus(const std::string& status) {
    return status == "Ready";
}

bool IsRoutineActionStatus(const std::string& status) {
    if (status.empty()
        || status == "Script packages refreshed."
        || status == "Stop requested.") {
        return true;
    }
    return status.rfind("Started ", 0) == 0;
}

bool IsErrorActionStatus(const std::string& status) {
    return status.rfind("Start failed:", 0) == 0
        || status.rfind("Selected package is invalid.", 0) == 0;
}

bool HasVisibleRuntimeDetail(const Gui::State::ScriptModelSnapshot& snapshot,
                             const Gui::State::ScriptPackageInfo* package) {
    if (snapshot.lastResult.stopReason != Scripting::ScriptStopReason::None) {
        return true;
    }
    if (snapshot.audit.recordCount > 0) {
        return true;
    }
    return package != nullptr
        && (package->effectiveProfile != "Safe"
            || (!package->requestedProfile.empty()
                && package->requestedProfile != package->effectiveProfile));
}

bool HasVisiblePackageStatus(const Gui::State::ScriptPackageInfo* package) {
    if (package == nullptr) {
        return false;
    }
    return !package->valid || !IsPackageReadyStatus(package->statusMessage);
}

int EstimateTopStatusLines(const Gui::State::ScriptModelSnapshot& snapshot,
                           const Gui::State::ScriptPackageInfo* package) {
    int lines = 2; // Toolbar/status row + package row.
    if (HasVisibleRuntimeDetail(snapshot, package)) {
        ++lines;
    }
    if (HasVisiblePackageStatus(package)) {
        ++lines;
    }
    if (!snapshot.lastResult.message.empty()) {
        ++lines;
    }
    if (!IsRoutineActionStatus(snapshot.lastActionStatus)) {
        ++lines;
    }
    return (std::min)(lines, 5);
}

float EstimateTopStatusHeight(const Gui::State::ScriptModelSnapshot& snapshot,
                              const Gui::State::ScriptPackageInfo* package) {
    const float toolbarHeight = ImGui::GetFrameHeightWithSpacing();
    const float textHeight = ImGui::GetTextLineHeightWithSpacing();
    const float padding = ImGui::GetStyle().FramePadding.y;
    const int estimatedLines = EstimateTopStatusLines(snapshot, package);
    const int textLines = (std::max)(estimatedLines - 1, 1);
    const int cappedTextLines = (std::min)(textLines, 4);
    return toolbarHeight + textHeight * static_cast<float>(cappedTextLines) + padding;
}

void TextWarning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    ImGui::TextColoredV(kWarningColor, format, args);
    va_end(args);
}

void TextError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    ImGui::TextColoredV(kErrorColor, format, args);
    va_end(args);
}

void TextWrappedWarning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
    ImGui::TextWrappedV(format, args);
    ImGui::PopStyleColor();
    va_end(args);
}

void TextWrappedError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
    ImGui::TextWrappedV(format, args);
    ImGui::PopStyleColor();
    va_end(args);
}

void RenderAttentionMessages(const Gui::State::ScriptModelSnapshot& snapshot) {
    if (!snapshot.lastResult.message.empty()) {
        if (IsFailedRunState(snapshot.lastResult.runState)) {
            TextWrappedError("Result: %s", snapshot.lastResult.message.c_str());
        } else {
            TextWrappedWarning("Result: %s", snapshot.lastResult.message.c_str());
        }
    }

    if (!IsRoutineActionStatus(snapshot.lastActionStatus)) {
        if (IsErrorActionStatus(snapshot.lastActionStatus)) {
            TextWrappedError("Action: %s", snapshot.lastActionStatus.c_str());
        } else {
            TextWrappedWarning("Action: %s", snapshot.lastActionStatus.c_str());
        }
    }
}

const Gui::State::ScriptPackageInfo* GetSelectedPackage(
    const Gui::State::ScriptModelSnapshot& snapshot) {
    if (snapshot.selectedPackageIndex < 0
        || snapshot.selectedPackageIndex >= static_cast<int>(snapshot.packages.size())) {
        return nullptr;
    }
    return &snapshot.packages[static_cast<size_t>(snapshot.selectedPackageIndex)];
}

void RenderModsRootPath(const Gui::State::ScriptModelSnapshot& snapshot) {
    if (snapshot.modsRootUtf8.empty()) {
        return;
    }

    ImGui::TextDisabled("./stalker_runtime/mods");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", snapshot.modsRootUtf8.c_str());
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Copy##modsRoot")) {
        ImGui::SetClipboardText(snapshot.modsRootUtf8.c_str());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Copy full path");
    }
}

void RenderPackageList(ControlPanelSessionState& state,
                       const Gui::State::ScriptModelSnapshot& snapshot) {
    if (ImGui::Button("Refresh##scripting")) {
        state.scriptModel.RefreshPackages();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%zu package%s",
                        snapshot.packages.size(),
                        snapshot.packages.size() == 1 ? "" : "s");
    RenderModsRootPath(snapshot);

    const float listHeight = (std::max)(ImGui::GetContentRegionAvail().y, 1.0f);
    if (!ImGui::BeginChild("ScriptingPackages", ImVec2(0, listHeight), true)) {
        ImGui::EndChild();
        return;
    }

    if (snapshot.packages.empty()) {
        ImGui::TextWrapped("No script packages found. Add packages under stalker_runtime/mods and click Refresh.");
        ImGui::EndChild();
        return;
    }

    for (size_t i = 0; i < snapshot.packages.size(); ++i) {
        const auto& package = snapshot.packages[i];
        ImGui::PushID(static_cast<int>(i));

        const bool selected = snapshot.selectedPackageIndex == static_cast<int>(i);
        std::string label = package.displayName.empty() ? package.relativeScriptPathUtf8 : package.displayName;
        if (!package.valid) {
            label += " *";
        }
        if (ImGui::Selectable(label.c_str(), selected)) {
            state.scriptModel.SelectPackage(i);
        }
        if (!package.valid && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", package.statusMessage.c_str());
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
}

void RenderActionToolbar(ControlPanelSessionState& state,
                         const Gui::State::ScriptModelSnapshot& snapshot,
                         const Gui::State::ScriptPackageInfo* package) {
    const bool active = IsActiveRunState(snapshot.runState);
    const bool canStart = package != nullptr && package->valid && !active;
    const bool canReload = package != nullptr && package->valid &&
        (snapshot.runState == Scripting::ScriptRunState::Running || !active);

    ImGui::BeginDisabled(!canStart);
    if (ImGui::Button("Start##scripting")) {
        state.scriptModel.StartSelected();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!active);
    if (ImGui::Button("Stop##scripting")) {
        state.scriptModel.StopActive();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!canReload);
    if (ImGui::Button("Reload##scripting")) {
        state.scriptModel.ReloadSelected();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Clear console##scripting")) {
        state.scriptModel.ClearConsole();
    }

    ImGui::SameLine();
    if (IsFailedRunState(snapshot.runState)) {
        TextError("State: %s", RunStateToText(snapshot.runState));
    } else if (IsWarningRunState(snapshot.runState)) {
        TextWarning("State: %s", RunStateToText(snapshot.runState));
    } else {
        ImGui::TextDisabled("State: %s", RunStateToText(snapshot.runState));
    }

    ImGui::SameLine();
    if (Scripting::IsOk(snapshot.lastResult.status)) {
        ImGui::TextDisabled("Last: %s", Scripting::StatusToString(snapshot.lastResult.status));
    } else {
        TextWarning("Last: %s", Scripting::StatusToString(snapshot.lastResult.status));
    }
}

void RenderRuntimeStatusLine(const Gui::State::ScriptModelSnapshot& snapshot,
                             const Gui::State::ScriptPackageInfo* package) {
    bool renderedAny = false;
    if (snapshot.lastResult.stopReason != Scripting::ScriptStopReason::None) {
        ImGui::TextDisabled("Stop: %s", StopReasonToText(snapshot.lastResult.stopReason));
        renderedAny = true;
    }

    if (package != nullptr
        && (package->effectiveProfile != "Safe"
            || (!package->requestedProfile.empty()
                && package->requestedProfile != package->effectiveProfile))) {
        if (renderedAny) {
            ImGui::SameLine();
        }
        TextWarning("Profile: %s", package->effectiveProfile.c_str());
        if (!package->requestedProfile.empty() && package->requestedProfile != package->effectiveProfile) {
            ImGui::SameLine();
            TextWarning("(requested %s)", package->requestedProfile.c_str());
        }
        renderedAny = true;
    }

    if (!renderedAny) {
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
}

void RenderAuditStatusLine(const Gui::State::ScriptModelSnapshot& snapshot) {
    const auto& counters = snapshot.audit.counters;
    if (snapshot.audit.recordCount == 0 &&
        counters.reads == 0 && counters.writes == 0 && counters.invokes == 0) {
        return;
    }

    const uint64_t elapsedMs =
        snapshot.audit.lastEventTickMs > snapshot.audit.firstEventTickMs
            ? snapshot.audit.lastEventTickMs - snapshot.audit.firstEventTickMs
            : 0;
    const double elapsedSeconds = elapsedMs > 0 ? static_cast<double>(elapsedMs) / 1000.0 : 1.0;
    const double writesPerSecond = static_cast<double>(counters.writes) / elapsedSeconds;
    const double invokesPerSecond = static_cast<double>(counters.invokes) / elapsedSeconds;

    ImGui::TextDisabled("Audit: W %.1f/s (%llu)  I %.1f/s (%llu)  R %llu  stale %llu  timeouts %llu",
                        writesPerSecond,
                        static_cast<unsigned long long>(counters.writes),
                        invokesPerSecond,
                        static_cast<unsigned long long>(counters.invokes),
                        static_cast<unsigned long long>(counters.reads),
                        static_cast<unsigned long long>(counters.staleHandles),
                        static_cast<unsigned long long>(counters.timeouts));
}

void RenderPackageMetadataTooltip(const Gui::State::ScriptPackageInfo& package) {
    if (!ImGui::IsItemHovered()) {
        return;
    }

    ImGui::BeginTooltip();
    ImGui::Text("Entry: %s", package.entryFile.c_str());
    ImGui::Text("Path: %s", package.relativeScriptPathUtf8.c_str());
    ImGui::Text("Manifest: %s", package.hasManifest ? "Yes" : "No");
    ImGui::Text("Profile: %s", package.effectiveProfile.c_str());
    ImGui::Text("Tick: %u ms", package.tickIntervalMs);
    ImGui::Text("Command timeout: %u ms", package.commandTimeoutMs);
    ImGui::Text("Soft timeout: %u ms", package.softTimeoutMs);
    ImGui::Text("Hard quarantine: %u ms", package.hardQuarantineMs);
    if (!package.requestedProfile.empty() && package.requestedProfile != package.effectiveProfile) {
        ImGui::Text("Requested profile: %s", package.requestedProfile.c_str());
    }
    if (!package.version.empty()) {
        ImGui::Text("Version: %s", package.version.c_str());
    }
    if (!package.author.empty()) {
        ImGui::Text("Author: %s", package.author.c_str());
    }
    ImGui::EndTooltip();
}

void RenderPackageStatusLine(const Gui::State::ScriptPackageInfo* package) {
    if (package == nullptr) {
        ImGui::TextUnformatted("Select a script package.");
        return;
    }

    ImGui::Text("Package: %s", package->displayName.c_str());
    RenderPackageMetadataTooltip(*package);

    if (!package->valid) {
        TextError("Invalid: %s", package->statusMessage.c_str());
    } else if (!IsPackageReadyStatus(package->statusMessage)) {
        TextWarning("Status: %s", package->statusMessage.c_str());
    }
}

void RenderTopStatusArea(ControlPanelSessionState& state,
                         const Gui::State::ScriptModelSnapshot& snapshot,
                         const Gui::State::ScriptPackageInfo* package,
                         float height) {
    if (!ImGui::BeginChild("ScriptingTopStatus", ImVec2(0, height), false)) {
        ImGui::EndChild();
        return;
    }

    RenderActionToolbar(state, snapshot, package);
    RenderRuntimeStatusLine(snapshot, package);
    RenderAuditStatusLine(snapshot);
    RenderPackageStatusLine(package);
    RenderAttentionMessages(snapshot);

    ImGui::EndChild();
}

void RenderConsole(const Gui::State::ScriptModelSnapshot& snapshot, float height) {
    ImGui::TextDisabled("Console (%zu / %zu)",
                        snapshot.consoleLineCount,
                        Gui::State::ScriptModel::kMaxConsoleLines);

    const float consoleHeight =
        (std::max)(height - ImGui::GetTextLineHeightWithSpacing(), 1.0f);
    if (!ImGui::BeginChild("ScriptingConsole", ImVec2(0, consoleHeight), true, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return;
    }

    if (snapshot.consoleLines.empty()) {
        ImGui::TextUnformatted("Console is empty.");
    } else {
        for (const auto& line : snapshot.consoleLines) {
            ImGui::TextUnformatted(line.c_str());
        }

        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }

    ImGui::EndChild();
}
} // namespace

void RenderScriptingTab(ControlPanelSessionState& state) {
    Gui::State::ScriptModelSnapshot snapshot = state.scriptModel.Snapshot();
    if (snapshot.modsRootUtf8.empty() && snapshot.packages.empty() && snapshot.lastActionStatus.empty()) {
        state.scriptModel.RefreshPackages();
        snapshot = state.scriptModel.Snapshot();
    }

    const Gui::State::ScriptPackageInfo* selectedPackage = GetSelectedPackage(snapshot);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float paneHeight = (std::max)(avail.y, 1.0f);
    const float leftWidth = (std::max)(avail.x * 0.28f, 180.0f);
    const float rightWidth =
        (std::max)(avail.x - leftWidth - style.ItemSpacing.x, 120.0f);

    if (ImGui::BeginChild("ScriptingLeftPane", ImVec2(leftWidth, paneHeight), true)) {
        RenderPackageList(state, snapshot);
    }
    ImGui::EndChild();

    ImGui::SameLine(0.0f, style.ItemSpacing.x);

    if (ImGui::BeginChild("ScriptingRightPane", ImVec2(rightWidth, paneHeight), true)) {
        const float rightAvailY = ImGui::GetContentRegionAvail().y;
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const float desiredSummaryHeight = EstimateTopStatusHeight(snapshot, selectedPackage);
        const float minConsoleHeight = lineHeight * 6.0f;
        float topStatusHeight = desiredSummaryHeight;

        if (rightAvailY > minConsoleHeight + style.ItemSpacing.y) {
            topStatusHeight = (std::min)(desiredSummaryHeight,
                                         rightAvailY - minConsoleHeight - style.ItemSpacing.y);
        } else {
            topStatusHeight = (std::max)(rightAvailY * 0.35f, 1.0f);
        }

        RenderTopStatusArea(state, snapshot, selectedPackage, topStatusHeight);
        ImGui::Separator();

        const float consoleAreaHeight = (std::max)(ImGui::GetContentRegionAvail().y, 1.0f);
        RenderConsole(snapshot, consoleAreaHeight);
    }
    ImGui::EndChild();
}
} // namespace Gui::Views

#endif // ENABLE_DUMPER
