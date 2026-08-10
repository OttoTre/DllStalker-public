#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "types/value_search_types.h"

namespace Gui::State
{
// Workers write under mutex; UI TakeSnapshot()s. Hits are shared_ptr (cheap copy).
struct ValueSearchModel {
    mutable std::mutex mutex{};

    std::atomic<uint64_t> searchId{0};
    bool                  inProgress = false;
    bool                  isDrill    = false;
    // Drill cancel restore only — shared baseline, not a full params copy.
    std::shared_ptr<const std::vector<Engine::ValueSearchHit>> drillBaselineHits{};
    std::shared_ptr<const std::vector<Engine::ValueSearchHit>> hits =
        std::make_shared<const std::vector<Engine::ValueSearchHit>>();
    // Bumped when hits identity changes (Publish / BeginRun clear / cancel restore).
    // Label cache keys on this — not ptr+size (equal-count Publish can reuse addresses).
    uint64_t hitsGeneration = 0;
    Engine::ValueSearchTruncReason truncReason = Engine::ValueSearchTruncReason::None;
    std::shared_ptr<const std::string> statusMessage = std::make_shared<const std::string>();
    int selectedHitIndex = -1;

    // GUI-thread only.
    bool chipNumber = true;
    bool chipString = true;
    bool chipPtr    = true;
    bool chipDeep   = false;
    bool nameMatchStrict  = false; // ~ fuzzy / = strict
    bool valueMatchStrict = true;

    struct Snapshot {
        uint64_t searchId = 0;
        bool inProgress = false;
        bool isDrill = false;
        std::shared_ptr<const std::vector<Engine::ValueSearchHit>> hits{};
        uint64_t hitsGeneration = 0;
        Engine::ValueSearchTruncReason truncReason = Engine::ValueSearchTruncReason::None;
        std::shared_ptr<const std::string> statusMessage{};
        int selectedHitIndex = -1;
    };

    Snapshot TakeSnapshot() const {
        std::lock_guard<std::mutex> lock(mutex);
        Snapshot snap{};
        snap.searchId = searchId.load(std::memory_order_relaxed);
        snap.inProgress = inProgress;
        snap.isDrill = isDrill;
        snap.hits = hits;
        snap.hitsGeneration = hitsGeneration;
        snap.truncReason = truncReason;
        snap.statusMessage = statusMessage;
        snap.selectedHitIndex = selectedHitIndex;
        return snap;
    }

    void BeginRun(uint64_t id, bool drill,
                  std::shared_ptr<const std::vector<Engine::ValueSearchHit>> baselineHits = {}) {
        std::lock_guard<std::mutex> lock(mutex);
        searchId.store(id, std::memory_order_relaxed);
        inProgress = true;
        isDrill = drill;
        if (drill) {
            drillBaselineHits = std::move(baselineHits);
        }
        else {
            drillBaselineHits.reset();
            hits = std::make_shared<const std::vector<Engine::ValueSearchHit>>();
            ++hitsGeneration;
            selectedHitIndex = -1;
        }
        truncReason = Engine::ValueSearchTruncReason::None;
        statusMessage = std::make_shared<const std::string>(
            drill ? "Drilling..." : "Searching...");
    }

    void Publish(uint64_t id, Engine::ValueSearchScanResult result, std::string status) {
        std::lock_guard<std::mutex> lock(mutex);
        if (searchId.load(std::memory_order_relaxed) != id) {
            return;
        }
        hits = std::make_shared<const std::vector<Engine::ValueSearchHit>>(std::move(result.hits));
        ++hitsGeneration;
        truncReason = result.truncReason;
        statusMessage = std::make_shared<const std::string>(std::move(status));
        inProgress = false;
        selectedHitIndex = hits->empty() ? -1 : 0;
    }

    void MarkCancelled(uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex);
        if (searchId.load(std::memory_order_relaxed) != id) {
            return;
        }
        // Worker may have Publish'd before join — do not clobber status/hits.
        if (!inProgress) {
            return;
        }
        inProgress = false;
        if (isDrill && hits->empty() && drillBaselineHits && !drillBaselineHits->empty()) {
            hits = drillBaselineHits;
            ++hitsGeneration;
            selectedHitIndex = hits->empty() ? -1 : 0;
        }
        statusMessage = std::make_shared<const std::string>("Cancelled");
    }
};
} // namespace Gui::State

#endif // ENABLE_DUMPER
