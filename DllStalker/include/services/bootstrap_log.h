#pragma once

#include <cstdarg>
#include <cstddef>
#include <string>
#include <vector>

namespace Engine::Services::BootstrapLog
{
constexpr size_t kMaxLines = 300;

// Thread-safe ring buffer for bootstrap / hook-install diagnostics.
// Dumper builds: in-memory only (GUI snapshots on the Init screen).
// Release builds: also appends to stalker_runtime/dllstalker.log next to the proxy DLL.
void Write(const char* fmt, ...);

// Open / truncate the Release file sink. No-op when ENABLE_DUMPER is defined.
void EnsureFileSink();

std::vector<std::string> Snapshot();
} // namespace Engine::Services::BootstrapLog
