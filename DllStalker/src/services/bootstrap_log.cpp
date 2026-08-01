#include "pch.h"

#include "services/bootstrap_log.h"

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

#ifndef ENABLE_DUMPER
#include <filesystem>
#include <fstream>

#include "services/module_path.h"
#endif

namespace Engine::Services::BootstrapLog
{
namespace
{
std::mutex              g_mutex;
std::deque<std::string> g_lines;

#ifndef ENABLE_DUMPER
std::ofstream g_file;
bool          g_fileReady = false;
int           g_fileOpenAttempts = 0;
constexpr int kMaxFileOpenAttempts = 5;

void OpenFileSinkLocked() {
    if (g_fileReady || g_fileOpenAttempts >= kMaxFileOpenAttempts) {
        return;
    }
    ++g_fileOpenAttempts;

    const std::filesystem::path runtimeDir =
        std::filesystem::path(GetProxyDllDirectory()) / L"stalker_runtime";
    std::error_code ec;
    std::filesystem::create_directories(runtimeDir, ec);

    const std::filesystem::path logPath = runtimeDir / L"dllstalker.log";
    if (g_file.is_open()) {
        g_file.close();
    }
    g_file.open(logPath, std::ios::out | std::ios::trunc);
    if (g_file.is_open()) {
        g_fileReady = true;
    }
}
#endif

void PushLineLocked(std::string line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    if (line.empty()) {
        return;
    }

    g_lines.push_back(line);
    while (g_lines.size() > kMaxLines) {
        g_lines.pop_front();
    }

#ifndef ENABLE_DUMPER
    OpenFileSinkLocked();
    if (g_file.is_open()) {
        g_file << line << '\n';
        g_file.flush();
    }
#endif
}

void WriteV(const char* fmt, va_list args) {
    if (!fmt) {
        return;
    }

    char buffer[1024]{};
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    std::lock_guard<std::mutex> lock(g_mutex);
    PushLineLocked(buffer);
}
} // namespace

void Write(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    WriteV(fmt, args);
    va_end(args);
}

void EnsureFileSink() {
#ifndef ENABLE_DUMPER
    std::lock_guard<std::mutex> lock(g_mutex);
    OpenFileSinkLocked();
#endif
}

std::vector<std::string> Snapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return std::vector<std::string>(g_lines.begin(), g_lines.end());
}
} // namespace Engine::Services::BootstrapLog
