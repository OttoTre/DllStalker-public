#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace Scripting
{
struct ScriptInstanceId {
    uint32_t id = 0;
    uint32_t generation = 0;

    bool operator==(const ScriptInstanceId&) const = default;
};

struct CancellationState {
    std::atomic<bool> cancelRequested{false};
    mutable std::mutex mutex{};
    mutable std::condition_variable cv{};

    CancellationState() = default;
    CancellationState(const CancellationState&) = delete;
    CancellationState& operator=(const CancellationState&) = delete;
    CancellationState(CancellationState&&) = delete;
    CancellationState& operator=(CancellationState&&) = delete;

    void RequestCancel() noexcept {
        cancelRequested.store(true, std::memory_order_release);
        cv.notify_all();
    }

    bool IsCancelled() const noexcept {
        return cancelRequested.load(std::memory_order_acquire);
    }

    void Reset() noexcept {
        cancelRequested.store(false, std::memory_order_release);
    }

    bool WaitForCancelOrTimeout(std::chrono::milliseconds duration) const {
        if (IsCancelled()) {
            return true;
        }
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, duration, [this] {
            return IsCancelled();
        });
    }
};

// Output sink for script console/log lines. Implemented by runtime/GUI layers.
class IOutputSink {
public:
    virtual ~IOutputSink() = default;
    virtual void Append(std::string_view line) = 0;
};

} // namespace Scripting

#endif // ENABLE_DUMPER
