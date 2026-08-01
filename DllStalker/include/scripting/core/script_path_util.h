#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <filesystem>
#include <string>

namespace Scripting
{
std::string Utf8FromWide(const std::wstring& text);
std::filesystem::path ToPath(const std::wstring& text);
bool IsPathInsideRoot(const std::filesystem::path& root, const std::filesystem::path& candidate);
} // namespace Scripting

#endif // ENABLE_DUMPER
