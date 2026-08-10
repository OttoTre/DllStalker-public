#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <memory>
#include <mutex>
#include <vector>

#include "types/dumper_types.h"

namespace Gui::State
{
// Mutex-guarded class list for the currently selected image. Same shape as
// ImageCacheModel; kept distinct so swap-order between image / class loads
// is unambiguous. Snapshot returns shared_ptr (cheap per-frame copy).
struct ClassCacheModel
{
    std::shared_ptr<const std::vector<Engine::ClassInfo>> data =
        std::make_shared<const std::vector<Engine::ClassInfo>>();
    mutable std::mutex mutex{};

    void Clear();
    std::shared_ptr<const std::vector<Engine::ClassInfo>> Snapshot() const;
    void Replace(std::vector<Engine::ClassInfo> v);
};
} // namespace Gui::State

#endif // ENABLE_DUMPER
