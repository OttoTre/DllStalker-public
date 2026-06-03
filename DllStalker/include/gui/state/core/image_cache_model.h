#pragma once

#include "pch.h"

#include <mutex>
#include <vector>

#include "types/dumper_types.h"

namespace Gui::State
{
// Mutex-guarded snapshot of the image picker's row source. Worker threads
// publish into `data` while the GUI thread takes lock-free Snapshots() each
// frame.
struct ImageCacheModel
{
    std::vector<Engine::ImageInfo> data{};
    mutable std::mutex             mutex{};

    void Clear();
    std::vector<Engine::ImageInfo> Snapshot() const;
    void Replace(std::vector<Engine::ImageInfo> v);
};
} // namespace Gui::State
