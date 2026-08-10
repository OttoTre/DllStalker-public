#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <string>

namespace Engine::Types
{
// Dumper-safe name match (do not pull Gui::Infra::SearchFilter).
std::string ToLowerAscii(const std::string& str);
bool NameFuzzyMatch(const std::string& haystack, const std::string& needle);  // subsequence
bool NameStrictMatch(const std::string& haystack, const std::string& needle); // contiguous
} // namespace Engine::Types

#endif // ENABLE_DUMPER
