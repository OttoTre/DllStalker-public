#include "pch.h"

#ifdef ENABLE_DUMPER

#include <exception>
#include <filesystem>
#include <string>

#include "scripting/script_engine.h"
#include "scripting/core/script_sandbox.h"
#include "services/main_thread_dispatcher.h"

namespace Scripting
{
namespace
{
HMODULE GetSelfModuleHandle() noexcept {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&GetSelfModuleHandle),
                       &module);
    return module;
}

std::wstring GetProxyDllDirectory() {
    const HMODULE module = GetSelfModuleHandle();
    if (module == nullptr) {
        return L".";
    }

    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L".";
    }

    std::wstring directory(path, length);
    const size_t slash = directory.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        directory.resize(slash);
    }
    return directory;
}

std::filesystem::path ToPath(const std::wstring& text) {
    return std::filesystem::path(text);
}

bool IsPathInsideRoot(const std::filesystem::path& root, const std::filesystem::path& candidate) {
    std::error_code errorCode;
    const auto rootCanonical = std::filesystem::weakly_canonical(root, errorCode);
    if (errorCode) {
        return false;
    }
    const auto candidateCanonical = std::filesystem::weakly_canonical(candidate, errorCode);
    if (errorCode) {
        return false;
    }

    auto rootString = rootCanonical.wstring();
    auto candidateString = candidateCanonical.wstring();
    if (!rootString.empty() && rootString.back() != L'\\' && rootString.back() != L'/') {
        rootString.push_back(L'\\');
    }
    if (candidateString.size() < rootString.size()) {
        return false;
    }
    return _wcsnicmp(candidateString.c_str(), rootString.c_str(), rootString.size()) == 0;
}

struct ResolvedScriptPaths {
    std::wstring entryFilePath;
    std::wstring packageRoot;
    bool ok = false;
};

ResolvedScriptPaths ResolveScriptPaths(const std::wstring& relativeScriptPath) {
    ResolvedScriptPaths resolved{};
    if (relativeScriptPath.empty()) {
        return resolved;
    }

    const std::filesystem::path modsRoot = ToPath(GetProxyDllDirectory()) / L"stalker_runtime" / L"mods";
    const std::filesystem::path candidate = modsRoot / ToPath(relativeScriptPath);
    if (!IsPathInsideRoot(modsRoot, candidate)) {
        return resolved;
    }

    std::error_code errorCode;
    if (!std::filesystem::exists(candidate, errorCode) || errorCode) {
        return resolved;
    }

    const std::filesystem::path canonical = std::filesystem::weakly_canonical(candidate, errorCode);
    if (errorCode) {
        return resolved;
    }

    if (std::filesystem::is_directory(canonical, errorCode) && !errorCode) {
        resolved.packageRoot = canonical.wstring();
        const std::filesystem::path entryFile = canonical / L"main.lua";
        if (!IsPathInsideRoot(modsRoot, entryFile) || !std::filesystem::exists(entryFile, errorCode) || errorCode) {
            return resolved;
        }
        resolved.entryFilePath = entryFile.wstring();
    } else {
        resolved.entryFilePath = canonical.wstring();
        resolved.packageRoot = canonical.parent_path().wstring();
    }
    resolved.ok = true;
    return resolved;
}

ScriptRuntimeResult MakeWorkerExceptionResult(const char* message) {
    ScriptRuntimeResult result{};
    result.runState = ScriptRunState::Failed;
    result.status = DS_Status::DS_ERR_BAD_ARGUMENT;
    result.message = message != nullptr ? message : "script worker exception";
    return result;
}

bool IsActiveRunState(ScriptRunState state) noexcept {
    return state == ScriptRunState::Running ||
           state == ScriptRunState::Stopping ||
           state == ScriptRunState::Reloading ||
           state == ScriptRunState::Unresponsive;
}
} // namespace

ScriptEngine::ScriptEngine()
    : reflectionApi_(Engine::Unity, handleRegistry_)
{
    reflectionApi_.SetCommandChannel(&commandChannel_);
    reflectionApi_.SetAuditSink(this);
}

ScriptEngine::~ScriptEngine() {
    RequestStop(ScriptStopReason::Shutdown);
}

DS_Status ScriptEngine::Start(const ScriptStartOptions& options) {
    if (options.relativeScriptPath.empty() || options.outputSink == nullptr) {
        return DS_Status::DS_ERR_BAD_ARGUMENT;
    }

    const ResolvedScriptPaths resolved = ResolveScriptPaths(options.relativeScriptPath);
    if (!resolved.ok) {
        std::lock_guard<std::mutex> lock(mutex_);
        lastResult_ = ScriptRuntimeResult{};
        lastResult_.runState = ScriptRunState::LoadError;
        lastResult_.status = DS_Status::DS_ERR_BAD_ARGUMENT;
        lastResult_.message = "script path is invalid or outside stalker_runtime/mods";
        runState_ = ScriptRunState::LoadError;
        return DS_Status::DS_ERR_BAD_ARGUMENT;
    }

    ScriptStartOptions workerOptions = options;
    workerOptions.resolvedEntryPath = resolved.entryFilePath;
    workerOptions.packageRoot = resolved.packageRoot;

    std::jthread finishedWorker;
    bool signalStop = false;
    bool recordCancel = false;
    bool queuedReload = false;
    ScriptInstanceId stoppedInstance{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (worker_.joinable()) {
            if (!workerFinished_.load(std::memory_order_acquire)) {
                pendingStart_ = workerOptions;
                requestedStopReason_ = ScriptStopReason::UserRequest;
                stoppedInstance = instanceId_;
                signalStop = true;
                if (runState_ == ScriptRunState::Running) {
                    recordCancel = true;
                }
                runState_ = ScriptRunState::Reloading;
                lastResult_.runState = ScriptRunState::Reloading;
                lastResult_.stopReason = ScriptStopReason::UserRequest;
                queuedReload = true;
            } else {
                finishedWorker = std::move(worker_);
            }
        }

        if (!queuedReload) {
            LaunchWorkerLocked(workerOptions);
        }
    }

    if (signalStop) {
        cancellation_.RequestCancel();
        commandChannel_.CancelAllPending(ScriptCommandStatus::CancelledShutdown);
    }
    if (recordCancel && stoppedInstance.id != 0) {
        ScriptAuditRecord record{};
        record.timestamp = GetTickCount64();
        record.scriptInstanceId = stoppedInstance;
        record.kind = ScriptAuditKind::Cancelled;
        record.status = DS_Status::DS_ERR_CANCELLED;
        RecordAudit(record);
    }

    if (finishedWorker.joinable()) {
        finishedWorker.join();
    }

    return DS_Status::DS_OK;
}

void ScriptEngine::RequestStop(ScriptStopReason reason) {
    cancellation_.RequestCancel();
    commandChannel_.CancelAllPending(ScriptCommandStatus::CancelledShutdown);

    std::jthread workerCopy;
    ScriptInstanceId stoppedInstance{};
    bool recordCancel = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requestedStopReason_ = reason;
        stoppedInstance = instanceId_;
        pendingStart_.reset();
        if (runState_ == ScriptRunState::Running) {
            runState_ = ScriptRunState::Stopping;
            lastResult_.runState = ScriptRunState::Stopping;
            lastResult_.stopReason = reason;
            recordCancel = true;
        } else if (runState_ == ScriptRunState::Reloading) {
            runState_ = ScriptRunState::Stopping;
            lastResult_.runState = ScriptRunState::Stopping;
            lastResult_.stopReason = reason;
        }

        if (reason == ScriptStopReason::Shutdown) {
            workerCopy = std::move(worker_);
        }
    }

    if (recordCancel && stoppedInstance.id != 0) {
        ScriptAuditRecord record{};
        record.timestamp = GetTickCount64();
        record.scriptInstanceId = stoppedInstance;
        record.kind = ScriptAuditKind::Cancelled;
        record.status = DS_Status::DS_ERR_CANCELLED;
        RecordAudit(record);
    }

    if (workerCopy.joinable()) {
        workerCopy.join();
    }
    if (reason == ScriptStopReason::Shutdown && stoppedInstance.id != 0) {
        handleRegistry_.InvalidateOwnedBy(stoppedInstance);
    }
}

void ScriptEngine::Pump() {
    std::jthread finishedWorker;
    std::optional<ScriptStartOptions> pendingStart;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!worker_.joinable() || !workerFinished_.load(std::memory_order_acquire)) {
            return;
        }

        finishedWorker = std::move(worker_);
        workerFinished_.store(false, std::memory_order_release);
        if (pendingStart_.has_value()) {
            pendingStart = std::move(pendingStart_);
            pendingStart_.reset();
        }
    }

    if (finishedWorker.joinable()) {
        finishedWorker.join();
    }

    if (pendingStart.has_value()) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!worker_.joinable()) {
            LaunchWorkerLocked(*pendingStart);
        }
    }
}

bool ScriptEngine::IsActive() const {
    return IsActiveRunState(GetRunState());
}

ScriptRunState ScriptEngine::GetRunState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (runState_ == ScriptRunState::Running &&
        unresponsive_.load(std::memory_order_acquire)) {
        return ScriptRunState::Unresponsive;
    }
    return runState_;
}

ScriptRuntimeResult ScriptEngine::GetLastResult() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastResult_;
}

ScriptAuditSnapshot ScriptEngine::GetAuditSnapshot() const {
    std::lock_guard<std::mutex> lock(auditMutex_);
    ScriptAuditSnapshot snapshot{};
    snapshot.counters = auditCounters_;
    snapshot.lastRecord = lastAuditRecord_;
    snapshot.firstEventTickMs = auditFirstEventTickMs_;
    snapshot.lastEventTickMs = auditLastEventTickMs_;
    snapshot.recordCount = auditRecords_.Count();
    snapshot.recordCapacity = auditRecords_.CapacityValue();
    return snapshot;
}

void ScriptEngine::RecordAudit(const ScriptAuditRecord& record) noexcept {
    std::lock_guard<std::mutex> lock(auditMutex_);
    auditRecords_.Push(record);
    lastAuditRecord_ = record;
    if (auditFirstEventTickMs_ == 0) {
        auditFirstEventTickMs_ = record.timestamp;
    }
    auditLastEventTickMs_ = record.timestamp;

    if (IsOk(record.status)) {
        switch (record.kind) {
        case ScriptAuditKind::Read:
            ++auditCounters_.reads;
            break;
        case ScriptAuditKind::Write:
            ++auditCounters_.writes;
            break;
        case ScriptAuditKind::Invoke:
            ++auditCounters_.invokes;
            break;
        default:
            break;
        }
    }

    switch (record.kind) {
    case ScriptAuditKind::SehFault:
        ++auditCounters_.faults;
        break;
    case ScriptAuditKind::StaleHandle:
        ++auditCounters_.staleHandles;
        break;
    case ScriptAuditKind::Timeout:
        ++auditCounters_.timeouts;
        break;
    case ScriptAuditKind::Cancelled:
        ++auditCounters_.cancellations;
        break;
    default:
        break;
    }

    if (record.kind != ScriptAuditKind::StaleHandle &&
        (record.status == DS_Status::DS_ERR_STALE_OBJECT ||
         record.status == DS_Status::DS_ERR_HANDLE_INVALID ||
         record.status == DS_Status::DS_ERR_HANDLE_KIND_MISMATCH)) {
        ++auditCounters_.staleHandles;
    } else if (record.kind != ScriptAuditKind::Timeout &&
               (record.status == DS_Status::DS_ERR_TIMEOUT ||
                record.status == DS_Status::DS_ERR_STARVATION)) {
        ++auditCounters_.timeouts;
    } else if (record.kind != ScriptAuditKind::Cancelled &&
               record.status == DS_Status::DS_ERR_CANCELLED) {
        ++auditCounters_.cancellations;
    } else if (record.kind != ScriptAuditKind::SehFault &&
               record.status == DS_Status::DS_ERR_INVALID_POINTER) {
        ++auditCounters_.faults;
    }
}

void ScriptEngine::ResetAudit() noexcept {
    std::lock_guard<std::mutex> lock(auditMutex_);
    auditCounters_ = ScriptAuditCounters{};
    auditRecords_ = ScriptAuditRingBuffer<>{};
    lastAuditRecord_ = ScriptAuditRecord{};
    auditFirstEventTickMs_ = 0;
    auditLastEventTickMs_ = 0;
}

void ScriptEngine::LaunchWorkerLocked(const ScriptStartOptions& options) {
    pendingStart_.reset();
    cancellation_.Reset();
    workerFinished_.store(false, std::memory_order_release);
    unresponsive_.store(false, std::memory_order_release);
    ResetAudit();
    requestedStopReason_ = ScriptStopReason::None;
    instanceId_.id += 1;
    instanceId_.generation += 1;
    const ScriptInstanceId workerInstanceId = instanceId_;

    runState_ = ScriptRunState::Running;
    lastResult_ = ScriptRuntimeResult{};
    lastResult_.runState = ScriptRunState::Running;

    worker_ = std::jthread([this, options, workerInstanceId](std::stop_token /*stopToken*/) {
        WorkerMain(options, workerInstanceId);
    });
}

void ScriptEngine::WorkerMain(ScriptStartOptions options, ScriptInstanceId workerInstanceId) noexcept {
    Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();

    ScriptRuntimeResult runtimeResult{};
    try {
        LuaScriptHostContext hostContext{};
        hostContext.instanceId = workerInstanceId;
        hostContext.cancellation = &cancellation_;
        hostContext.outputSink = options.outputSink;
        hostContext.sandboxPolicy = options.profile == ScriptProfile::Curated
                                  ? MakeDefaultCuratedPolicy()
                                  : MakeDefaultSafePolicy();
        hostContext.packageRoot = options.packageRoot;
        hostContext.entryFilePath = options.resolvedEntryPath;
        hostContext.instructionBudget = options.instructionBudget;
        hostContext.tickIntervalMs = options.tickIntervalMs;
        hostContext.commandTimeoutMs = options.commandTimeoutMs;
        hostContext.softTimeoutMs = options.softTimeoutMs;
        hostContext.hardQuarantineMs = options.hardQuarantineMs;
        hostContext.handleRegistry = &handleRegistry_;
        reflectionApi_.SetCommandTimeoutMs(options.commandTimeoutMs);
        hostContext.reflectionApi = &reflectionApi_;
        hostContext.commandChannel = &commandChannel_;
        hostContext.auditSink = this;
        hostContext.unresponsiveFlag = &unresponsive_;

        runtimeResult = RunScriptOnCurrentThread(hostContext);
    } catch (const std::exception& exception) {
        runtimeResult = MakeWorkerExceptionResult(exception.what());
    } catch (...) {
        runtimeResult = MakeWorkerExceptionResult("unknown script worker exception");
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!(instanceId_ == workerInstanceId)) {
            workerFinished_.store(true, std::memory_order_release);
            return;
        }

        lastResult_ = runtimeResult;
        if (requestedStopReason_ != ScriptStopReason::None) {
            lastResult_.stopReason = requestedStopReason_;
        } else if (cancellation_.IsCancelled()) {
            lastResult_.stopReason = ScriptStopReason::UserRequest;
        }
        runState_ = pendingStart_.has_value() ? ScriptRunState::Reloading : runtimeResult.runState;
    }

    handleRegistry_.InvalidateOwnedBy(workerInstanceId);
    unresponsive_.store(false, std::memory_order_release);
    workerFinished_.store(true, std::memory_order_release);
}

} // namespace Scripting

#endif // ENABLE_DUMPER
