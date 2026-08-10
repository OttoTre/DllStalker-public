#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/state/core/image_cache_model.h"

namespace Gui::State
{
void ImageCacheModel::Clear() {
    std::lock_guard<std::mutex> lock(mutex);
    data = std::make_shared<const std::vector<Engine::ImageInfo>>();
}

std::shared_ptr<const std::vector<Engine::ImageInfo>> ImageCacheModel::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    return data;
}

void ImageCacheModel::Replace(std::vector<Engine::ImageInfo> v) {
    std::lock_guard<std::mutex> lock(mutex);
    data = std::make_shared<const std::vector<Engine::ImageInfo>>(std::move(v));
}
} // namespace Gui::State

#endif // ENABLE_DUMPER
