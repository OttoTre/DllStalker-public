#include "pch.h"

#ifdef ENABLE_DUMPER

#include "types/name_fuzzy.h"

#include <algorithm>
#include <cctype>

namespace Engine::Types
{
std::string ToLowerAscii(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

bool NameFuzzyMatch(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (haystack.empty()) return false;

    size_t j = 0;
    for (size_t i = 0; i < haystack.size() && j < needle.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(haystack[i]))
            == std::tolower(static_cast<unsigned char>(needle[j]))) {
            ++j;
        }
    }
    return j == needle.size();
}

bool NameStrictMatch(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (haystack.empty() || haystack.size() < needle.size()) return false;

    const std::string h = ToLowerAscii(haystack);
    const std::string n = ToLowerAscii(needle);
    return h.find(n) != std::string::npos;
}
} // namespace Engine::Types

#endif // ENABLE_DUMPER
