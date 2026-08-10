#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/core/class_cache_model.h"

namespace Gui::State
{
void ClassCacheModel::Clear() {
    std::lock_guard<std::mutex> lock(mutex);
    data = std::make_shared<const std::vector<Engine::ClassInfo>>();
}

std::shared_ptr<const std::vector<Engine::ClassInfo>> ClassCacheModel::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    return data;
}

void ClassCacheModel::Replace(std::vector<Engine::ClassInfo> v) {
    std::lock_guard<std::mutex> lock(mutex);
    data = std::make_shared<const std::vector<Engine::ClassInfo>>(std::move(v));
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
