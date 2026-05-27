#pragma once

#include "pch.h"

#include <mutex>
#include <vector>

#include "types/dumper_types.h"

namespace Gui::State
{
// Mutex-guarded class list for the currently selected image. Same shape as
// ImageCacheModel; kept distinct so swap-order between image / class loads
// is unambiguous.
struct ClassCacheModel
{
    std::vector<Engine::ClassInfo> data{};
    mutable std::mutex             mutex{};

    void Clear();
    std::vector<Engine::ClassInfo> Snapshot() const;
    void Replace(std::vector<Engine::ClassInfo> v);
};
} // namespace Gui::State
