#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "scripting/script_engine.h"

namespace Gui::State
{
struct ScriptPackageInfo {
    std::string displayName{};
    std::string version{};
    std::string author{};
    std::string requestedProfile{};
    std::string effectiveProfile{ "Safe" };
    std::string relativeScriptPathUtf8{};
    std::string entryFile{};
    std::string statusMessage{};
    uint32_t    tickIntervalMs = 16;
    uint32_t    commandTimeoutMs = 1500;
    uint32_t    softTimeoutMs = 2000;
    uint32_t    hardQuarantineMs = 5000;
    bool        hasManifest = false;
    bool        valid = false;
};

struct ScriptModelSnapshot {
    std::vector<ScriptPackageInfo> packages{};
    std::vector<std::string>       consoleLines{};
    std::string                    modsRootUtf8{};
    std::string                    lastActionStatus{};
    Scripting::ScriptRunState      runState = Scripting::ScriptRunState::Idle;
    Scripting::ScriptRuntimeResult lastResult{};
    Scripting::ScriptAuditSnapshot audit{};
    int                            selectedPackageIndex = -1;
    size_t                         consoleLineCount = 0;
};

class ScriptModel final : public Scripting::IOutputSink {
public:
    static constexpr size_t kMaxConsoleLines = 500;

    ScriptModel() = default;
    ~ScriptModel() override;

    ScriptModel(const ScriptModel&) = delete;
    ScriptModel& operator=(const ScriptModel&) = delete;

    void Append(std::string_view line) override;

    void RefreshPackages();
    bool SelectPackage(size_t index);

    Scripting::DS_Status StartSelected();
    void StopActive(Scripting::ScriptStopReason reason = Scripting::ScriptStopReason::UserRequest);
    Scripting::DS_Status ReloadSelected();
    void Pump();
    bool IsRuntimeActive() const;

    void ClearConsole();

    ScriptModelSnapshot Snapshot() const;

private:
    bool TryGetSelectedPackage(ScriptPackageInfo& out) const;
    void PushConsoleLineLocked(std::string line);
    void SetLastActionStatus(std::string status);

    mutable std::mutex mutex_{};
    std::vector<ScriptPackageInfo> packages_{};
    std::deque<std::string> consoleLines_{};
    std::string modsRootUtf8_{};
    std::string lastActionStatus_{};
    int selectedPackageIndex_ = -1;

    Scripting::ScriptEngine engine_{};
};
} // namespace Gui::State

#endif // ENABLE_DUMPER
