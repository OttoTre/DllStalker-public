#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/inspector_bookmarks_model.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace Gui::State
{
namespace
{
std::string TrimCopy(const char* raw) {
    if (!raw) {
        return {};
    }
    std::string s(raw);
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}
} // namespace

bool InspectorBookmarksModel::Add(Bookmark entry) {
    const std::string trimmed = TrimCopy(entry.name.c_str());
    if (trimmed.empty()) {
        return false;
    }
    entry.name = trimmed;
    if (entry.id == 0) {
        entry.id = nextId++;
    }
    else if (entry.id >= nextId) {
        nextId = entry.id + 1;
    }
    bookmarks.insert(bookmarks.begin(), std::move(entry));
    return true;
}

bool InspectorBookmarksModel::Remove(uint32_t id) {
    auto it = std::find_if(bookmarks.begin(), bookmarks.end(),
                           [id](const Bookmark& b) { return b.id == id; });
    if (it == bookmarks.end()) {
        return false;
    }
    bookmarks.erase(it);
    return true;
}

bool InspectorBookmarksModel::Rename(uint32_t id, const char* newName) {
    const std::string trimmed = TrimCopy(newName);
    if (trimmed.empty()) {
        return false;
    }
    auto* entry = Find(id);
    if (!entry) {
        return false;
    }
    entry->name = trimmed;
    return true;
}

Bookmark* InspectorBookmarksModel::Find(uint32_t id) {
    auto it = std::find_if(bookmarks.begin(), bookmarks.end(),
                           [id](const Bookmark& b) { return b.id == id; });
    return it == bookmarks.end() ? nullptr : &*it;
}

const Bookmark* InspectorBookmarksModel::Find(uint32_t id) const {
    auto it = std::find_if(bookmarks.begin(), bookmarks.end(),
                           [id](const Bookmark& b) { return b.id == id; });
    return it == bookmarks.end() ? nullptr : &*it;
}

void InspectorBookmarksModel::Clear() {
    bookmarks.clear();
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
