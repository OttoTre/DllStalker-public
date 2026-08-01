#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/runtime/script_model.h"

#include "services/module_path.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Gui::State
{
namespace
{
struct ManifestData {
    int schemaVersion = 0;
    std::string name{};
    std::string version{};
    std::string author{};
    std::string entryFile{ "main.lua" };
    std::string profile{ "Safe" };
    uint32_t tickIntervalMs = 16;
    uint32_t commandTimeoutMs = 1500;
    uint32_t softTimeoutMs = 2000;
    uint32_t hardQuarantineMs = 5000;
};

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), needed);
    return out;
}

std::string PathToUtf8(const std::filesystem::path& path) {
    return WideToUtf8(path.wstring());
}

std::string ReadTextFile(const std::filesystem::path& path, bool& ok) {
    ok = false;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();
    if (text.size() >= 3
        && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    ok = true;
    return text;
}

void SkipWhitespace(const std::string& text, size_t& pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
        ++pos;
    }
}

bool LocateJsonValue(const std::string& text, const char* key, size_t& valuePos) {
    const std::string token = std::string("\"") + key + "\"";
    size_t keyPos = text.find(token);
    if (keyPos == std::string::npos) {
        return false;
    }

    keyPos += token.size();
    SkipWhitespace(text, keyPos);
    if (keyPos >= text.size() || text[keyPos] != ':') {
        valuePos = keyPos;
        return true;
    }
    ++keyPos;
    SkipWhitespace(text, keyPos);
    valuePos = keyPos;
    return true;
}

bool ParseJsonStringAt(const std::string& text, size_t& pos, std::string& out, std::string& error) {
    if (pos >= text.size() || text[pos] != '"') {
        error = "expected JSON string";
        return false;
    }
    ++pos;

    std::string value;
    while (pos < text.size()) {
        const char c = text[pos++];
        if (c == '"') {
            out = std::move(value);
            return true;
        }
        if (c == '\\') {
            if (pos >= text.size()) {
                error = "unterminated JSON escape";
                return false;
            }
            const char escaped = text[pos++];
            switch (escaped) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            default:
                error = "unsupported JSON escape";
                return false;
            }
            continue;
        }
        if (static_cast<unsigned char>(c) < 0x20) {
            error = "control character in JSON string";
            return false;
        }
        value.push_back(c);
    }

    error = "unterminated JSON string";
    return false;
}

bool ExtractOptionalString(const std::string& text,
                           const char* key,
                           std::string& out,
                           std::string& error) {
    size_t valuePos = 0;
    if (!LocateJsonValue(text, key, valuePos)) {
        return true;
    }
    return ParseJsonStringAt(text, valuePos, out, error);
}

bool ExtractRequiredInt(const std::string& text, const char* key, int& out, std::string& error) {
    size_t valuePos = 0;
    if (!LocateJsonValue(text, key, valuePos)) {
        error = std::string("missing ") + key;
        return false;
    }
    if (valuePos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[valuePos]))) {
        error = std::string("invalid ") + key;
        return false;
    }

    int value = 0;
    while (valuePos < text.size() && std::isdigit(static_cast<unsigned char>(text[valuePos])) != 0) {
        value = value * 10 + (text[valuePos] - '0');
        ++valuePos;
    }
    out = value;
    return true;
}

bool ExtractOptionalUInt(const std::string& text,
                         const char* key,
                         uint32_t& out,
                         uint32_t minValue,
                         uint32_t maxValue,
                         std::string& error) {
    size_t valuePos = 0;
    if (!LocateJsonValue(text, key, valuePos)) {
        return true;
    }
    if (valuePos >= text.size() || !std::isdigit(static_cast<unsigned char>(text[valuePos]))) {
        error = std::string("invalid ") + key;
        return false;
    }

    uint64_t value = 0;
    while (valuePos < text.size() && std::isdigit(static_cast<unsigned char>(text[valuePos])) != 0) {
        value = value * 10 + static_cast<uint64_t>(text[valuePos] - '0');
        ++valuePos;
    }
    if (value < minValue) {
        value = minValue;
    }
    if (value > maxValue) {
        value = maxValue;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

bool LoadManifest(const std::filesystem::path& manifestPath, ManifestData& manifest, std::string& error) {
    bool readOk = false;
    const std::string text = ReadTextFile(manifestPath, readOk);
    if (!readOk) {
        error = "manifest could not be read";
        return false;
    }

    if (!ExtractRequiredInt(text, "schema_version", manifest.schemaVersion, error)) {
        return false;
    }
    if (manifest.schemaVersion != 1) {
        error = "unsupported manifest schema";
        return false;
    }

    if (!ExtractOptionalString(text, "name", manifest.name, error)
        || !ExtractOptionalString(text, "version", manifest.version, error)
        || !ExtractOptionalString(text, "author", manifest.author, error)
        || !ExtractOptionalString(text, "entry_file", manifest.entryFile, error)
        || !ExtractOptionalString(text, "profile", manifest.profile, error)
        || !ExtractOptionalUInt(text, "tick_interval_ms", manifest.tickIntervalMs, 10, 1000, error)
        || !ExtractOptionalUInt(text, "command_timeout_ms", manifest.commandTimeoutMs, 100, 10000, error)
        || !ExtractOptionalUInt(text, "soft_timeout_ms", manifest.softTimeoutMs, 100, 10000, error)
        || !ExtractOptionalUInt(text, "hard_quarantine_ms", manifest.hardQuarantineMs, 500, 30000, error)) {
        return false;
    }

    if (manifest.entryFile.empty()) {
        manifest.entryFile = "main.lua";
    }
    if (manifest.profile.empty()) {
        manifest.profile = "Safe";
    }
    return true;
}

bool IsSafeEntryFileName(const std::filesystem::path& entryFile) {
    if (entryFile.empty()
        || entryFile.is_absolute()
        || entryFile.has_root_name()
        || entryFile.has_parent_path()) {
        return false;
    }

    const std::filesystem::path filename = entryFile.filename();
    return !filename.empty() && filename != L"." && filename != L"..";
}

bool EqualsIgnoreCase(std::string lhs, std::string rhs) {
    std::transform(lhs.begin(), lhs.end(), lhs.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::transform(rhs.begin(), rhs.end(), rhs.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return lhs == rhs;
}

Scripting::ScriptProfile ProfileFromDisplayName(const std::string& profile) {
    if (EqualsIgnoreCase(profile, "Curated") || EqualsIgnoreCase(profile, "Advanced")) {
        return Scripting::ScriptProfile::Curated;
    }
    return Scripting::ScriptProfile::Safe;
}

std::string EffectiveProfileName(const std::string& requestedProfile) {
    if (EqualsIgnoreCase(requestedProfile, "Curated") || EqualsIgnoreCase(requestedProfile, "Advanced")) {
        return "Curated";
    }
    return "Safe";
}

ScriptPackageInfo BuildDirectoryPackage(const std::filesystem::path& packageDir) {
    ScriptPackageInfo info{};
    info.displayName = PathToUtf8(packageDir.filename());
    info.entryFile = "main.lua";
    info.requestedProfile = "Safe";
    info.effectiveProfile = "Safe";

    const std::filesystem::path manifestPath = packageDir / L"manifest.json";
    std::error_code errorCode;
    if (std::filesystem::exists(manifestPath, errorCode) && !errorCode) {
        ManifestData manifest{};
        std::string manifestError;
        info.hasManifest = true;
        if (!LoadManifest(manifestPath, manifest, manifestError)) {
            info.statusMessage = "Invalid manifest: " + manifestError;
            return info;
        }

        if (!manifest.name.empty()) {
            info.displayName = manifest.name;
        }
        info.version = manifest.version;
        info.author = manifest.author;
        info.entryFile = manifest.entryFile;
        info.requestedProfile = manifest.profile;
        info.tickIntervalMs = manifest.tickIntervalMs;
        info.commandTimeoutMs = manifest.commandTimeoutMs;
        info.softTimeoutMs = manifest.softTimeoutMs;
        info.hardQuarantineMs = manifest.hardQuarantineMs;
    }
    info.effectiveProfile = EffectiveProfileName(info.requestedProfile);

    const std::filesystem::path entryRelative = Utf8ToWide(info.entryFile);
    if (!IsSafeEntryFileName(entryRelative)) {
        info.statusMessage = "entry_file must be a file name in the package root";
        return info;
    }

    const std::filesystem::path entryPath = packageDir / entryRelative;
    if (!std::filesystem::exists(entryPath, errorCode) || errorCode) {
        info.statusMessage = "entry file not found";
        return info;
    }
    if (!std::filesystem::is_regular_file(entryPath, errorCode) || errorCode) {
        info.statusMessage = "entry file is not a regular file";
        return info;
    }

    const std::filesystem::path relativeScriptPath = packageDir.filename() / entryRelative;
    info.relativeScriptPathUtf8 = PathToUtf8(relativeScriptPath);
    if (!EqualsIgnoreCase(info.requestedProfile, "Safe") && info.effectiveProfile == "Safe") {
        info.statusMessage = "Unsupported profile requested: " + info.requestedProfile + " (running as Safe)";
    } else {
        info.statusMessage = "Ready";
    }
    info.valid = true;
    return info;
}

ScriptPackageInfo BuildLooseLuaPackage(const std::filesystem::path& scriptPath) {
    ScriptPackageInfo info{};
    info.displayName = PathToUtf8(scriptPath.stem());
    info.entryFile = PathToUtf8(scriptPath.filename());
    info.requestedProfile = "Safe";
    info.effectiveProfile = "Safe";
    info.relativeScriptPathUtf8 = PathToUtf8(scriptPath.filename());
    info.statusMessage = "Ready";
    info.valid = true;
    return info;
}

bool PackageSortLess(const ScriptPackageInfo& lhs, const ScriptPackageInfo& rhs) {
    std::string left = lhs.displayName;
    std::string right = rhs.displayName;
    std::transform(left.begin(), left.end(), left.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    std::transform(right.begin(), right.end(), right.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return left < right;
}
} // namespace

ScriptModel::~ScriptModel() {
    engine_.RequestStop(Scripting::ScriptStopReason::Shutdown);
}

void ScriptModel::Append(std::string_view line) {
    if (line.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    PushConsoleLineLocked(std::string(line));
}

void ScriptModel::RefreshPackages() {
    const std::filesystem::path modsRoot = std::filesystem::path(Engine::Services::GetProxyDllDirectory())
        / L"stalker_runtime" / L"mods";
    const std::string modsRootUtf8 = PathToUtf8(modsRoot);

    std::string previousSelection;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (selectedPackageIndex_ >= 0 && selectedPackageIndex_ < static_cast<int>(packages_.size())) {
            previousSelection = packages_[static_cast<size_t>(selectedPackageIndex_)].relativeScriptPathUtf8;
        }
    }

    std::vector<ScriptPackageInfo> packages;
    std::string scanStatus;
    std::error_code errorCode;
    try {
        if (std::filesystem::exists(modsRoot, errorCode) && !errorCode
            && std::filesystem::is_directory(modsRoot, errorCode) && !errorCode) {
            for (const auto& entry : std::filesystem::directory_iterator(modsRoot, errorCode)) {
                if (errorCode) {
                    break;
                }

                if (entry.is_directory(errorCode) && !errorCode) {
                    packages.push_back(BuildDirectoryPackage(entry.path()));
                } else if (entry.is_regular_file(errorCode) && !errorCode
                           && entry.path().extension() == L".lua") {
                    packages.push_back(BuildLooseLuaPackage(entry.path()));
                }
            }
        }
    } catch (const std::exception& exception) {
        scanStatus = std::string("Package scan failed: ") + exception.what();
    }

    std::sort(packages.begin(), packages.end(), PackageSortLess);

    int selectedIndex = packages.empty() ? -1 : 0;
    if (!previousSelection.empty()) {
        for (size_t i = 0; i < packages.size(); ++i) {
            if (packages[i].relativeScriptPathUtf8 == previousSelection) {
                selectedIndex = static_cast<int>(i);
                break;
            }
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    packages_ = std::move(packages);
    modsRootUtf8_ = modsRootUtf8;
    selectedPackageIndex_ = selectedIndex;
    if (!scanStatus.empty()) {
        lastActionStatus_ = std::move(scanStatus);
    } else if (packages_.empty()) {
        lastActionStatus_ = "No script packages found.";
    } else {
        lastActionStatus_ = "Script packages refreshed.";
    }
}

bool ScriptModel::SelectPackage(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index >= packages_.size()) {
        return false;
    }
    selectedPackageIndex_ = static_cast<int>(index);
    return true;
}

Scripting::DS_Status ScriptModel::StartSelected() {
    ScriptPackageInfo package;
    if (!TryGetSelectedPackage(package)) {
        SetLastActionStatus("Select a script package first.");
        return Scripting::DS_Status::DS_ERR_BAD_ARGUMENT;
    }
    if (!package.valid) {
        SetLastActionStatus(package.statusMessage.empty() ? "Selected package is invalid." : package.statusMessage);
        return Scripting::DS_Status::DS_ERR_BAD_ARGUMENT;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        consoleLines_.clear();
    }

    Scripting::ScriptStartOptions options{};
    options.relativeScriptPath = Utf8ToWide(package.relativeScriptPathUtf8);
    options.profile = ProfileFromDisplayName(package.effectiveProfile);
    options.outputSink = this;
    options.tickIntervalMs = package.tickIntervalMs;
    options.commandTimeoutMs = package.commandTimeoutMs;
    options.softTimeoutMs = package.softTimeoutMs;
    options.hardQuarantineMs = package.hardQuarantineMs;

    const Scripting::DS_Status status = engine_.Start(options);
    if (Scripting::IsOk(status)) {
        SetLastActionStatus("Started " + package.displayName + ".");
    } else {
        SetLastActionStatus(std::string("Start failed: ") + Scripting::StatusToString(status));
    }
    return status;
}

void ScriptModel::StopActive(Scripting::ScriptStopReason reason) {
    engine_.RequestStop(reason);
    const Scripting::ScriptRuntimeResult result = engine_.GetLastResult();
    if (result.message.empty()) {
        SetLastActionStatus("Stop requested.");
    } else {
        SetLastActionStatus(result.message);
    }
}

Scripting::DS_Status ScriptModel::ReloadSelected() {
    return StartSelected();
}

void ScriptModel::Pump() {
    engine_.Pump();
}

bool ScriptModel::IsRuntimeActive() const {
    return engine_.IsActive();
}

void ScriptModel::ClearConsole() {
    std::lock_guard<std::mutex> lock(mutex_);
    consoleLines_.clear();
}

ScriptModelSnapshot ScriptModel::Snapshot() const {
    ScriptModelSnapshot snapshot{};
    snapshot.runState = engine_.GetRunState();
    snapshot.lastResult = engine_.GetLastResult();
    snapshot.audit = engine_.GetAuditSnapshot();

    std::lock_guard<std::mutex> lock(mutex_);
    snapshot.packages = packages_;
    snapshot.consoleLines.assign(consoleLines_.begin(), consoleLines_.end());
    snapshot.modsRootUtf8 = modsRootUtf8_;
    snapshot.lastActionStatus = lastActionStatus_;
    snapshot.selectedPackageIndex = selectedPackageIndex_;
    snapshot.consoleLineCount = consoleLines_.size();
    return snapshot;
}

bool ScriptModel::TryGetSelectedPackage(ScriptPackageInfo& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (selectedPackageIndex_ < 0 || selectedPackageIndex_ >= static_cast<int>(packages_.size())) {
        return false;
    }
    out = packages_[static_cast<size_t>(selectedPackageIndex_)];
    return true;
}

void ScriptModel::PushConsoleLineLocked(std::string line) {
    consoleLines_.push_back(std::move(line));
    while (consoleLines_.size() > kMaxConsoleLines) {
        consoleLines_.pop_front();
    }
}

void ScriptModel::SetLastActionStatus(std::string status) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastActionStatus_ = std::move(status);
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
