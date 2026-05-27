#pragma once

#include "pch.h"

#include <string>

#include "types/dumper_types.h"

namespace Gui::Infra::SearchFilter
{
std::string ToLowercase(const std::string& str);
bool FuzzyMatch(const std::string& haystack, const std::string& needle);
bool ClassMatches(const Engine::ClassInfo& classInfo, const std::string& lowerFilter);
bool MethodMatches(const Engine::MethodInfo& method, const std::string& lowerFilter);
bool FieldMatches(const Engine::FieldInfo& field, const std::string& lowerFilter);

} // namespace Gui::Infra::SearchFilter
