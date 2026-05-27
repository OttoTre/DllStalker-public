#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/infra/search_filter.h"

#include <algorithm>
#include <cctype>

namespace Gui::Infra::SearchFilter
{
std::string ToLowercase(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower;
}

bool FuzzyMatch(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (haystack.empty()) return false;

    size_t j = 0;
    for (size_t i = 0; i < haystack.size() && j < needle.size(); ++i) {
        if (std::tolower(haystack[i]) == std::tolower(needle[j])) {
            ++j;
        }
    }
    return j == needle.size();
}

bool ClassMatches(const Engine::ClassInfo& classInfo, const std::string& lowerFilter) {
    if (lowerFilter.empty()) return true;

    return FuzzyMatch(classInfo.name, lowerFilter) ||
           FuzzyMatch(classInfo.ns, lowerFilter);
}

bool MethodMatches(const Engine::MethodInfo& method, const std::string& lowerFilter) {
    if (lowerFilter.empty()) return true;

    return FuzzyMatch(method.name, lowerFilter) ||
           FuzzyMatch(method.returnType, lowerFilter) ||
           FuzzyMatch(method.parameters, lowerFilter);
}

bool FieldMatches(const Engine::FieldInfo& field, const std::string& lowerFilter) {
    if (lowerFilter.empty()) return true;

    return FuzzyMatch(field.name, lowerFilter) ||
           FuzzyMatch(field.type, lowerFilter) ||
           FuzzyMatch(field.valueDisplay, lowerFilter);
}

} // namespace Gui::Infra::SearchFilter

#endif // ENABLE_DUMPER
