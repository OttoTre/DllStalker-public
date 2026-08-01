#include "pch.h"

#ifdef ENABLE_DUMPER

#include "scripting/core/script_path_util.h"

namespace Scripting
{
std::string Utf8FromWide(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), needed, nullptr, nullptr);
    if (written <= 0) {
        return {};
    }
    if (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
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
} // namespace Scripting

#endif // ENABLE_DUMPER
