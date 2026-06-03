#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/history/inspector_history_model.h"

namespace Gui::State
{
static_assert(static_cast<int>(HistoryEntryKind::Navigation) == 0);
static_assert(static_cast<int>(HistoryEntryKind::MethodAudit) == 1);
static_assert(static_cast<int>(HistoryEntryKind::FieldAudit) == 2);

namespace
{
const NavigationSnapshot* GetNavigationPayload(const HistoryEntry& entry) {
    if (EntryKind(entry) != HistoryEntryKind::Navigation) {
        return nullptr;
    }
    return std::get_if<NavigationSnapshot>(&entry.payload);
}
} // namespace

void InspectorHistoryModel::Append(HistoryEntry entry) {
    if (EntryKind(entry) == HistoryEntryKind::Navigation) {
        const auto* nav = std::get_if<NavigationSnapshot>(&entry.payload);
        if (nav && !entries.empty() && EntryKind(entries.front()) == HistoryEntryKind::Navigation) {
            if (const auto* prev = GetNavigationPayload(entries.front());
                prev && NavigationFingerprintsEqual(*nav, *prev)) {
                return;
            }
        }
    }

    entries.push_front(std::move(entry));
    while (entries.size() > kMaxEntries) {
        entries.pop_back();
    }
}

void InspectorHistoryModel::Clear() {
    entries.clear();
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
