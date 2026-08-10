#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <memory>
#include <mutex>
#include <vector>

#include "types/dumper_types.h"

namespace Gui::State
{
// Mutex-guarded snapshot of the image picker's row source. Worker threads
// publish via Replace while the GUI thread takes cheap shared_ptr Snapshots
// each frame.
struct ImageCacheModel
{
    std::shared_ptr<const std::vector<Engine::ImageInfo>> data =
        std::make_shared<const std::vector<Engine::ImageInfo>>();
    mutable std::mutex mutex{};

    void Clear();
    std::shared_ptr<const std::vector<Engine::ImageInfo>> Snapshot() const;
    void Replace(std::vector<Engine::ImageInfo> v);
};
} // namespace Gui::State

#endif // ENABLE_DUMPER
