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
// GUI-stamped index/hit row. Not Engine::MethodInfo (no JIT / type enrichment).
struct MethodSearchRow {
    void*       klassPtr = nullptr;
    std::string classNs{};
    std::string className{};
    std::string methodName{};
    bool        isStatic = false;
    int         paramCount = -1;
    bool        paramCountKnown = false;
};

// Workers write under mutex; UI TakeSnapshot()s. Hits/index are shared_ptr (cheap copy).
struct MethodSearchModel {
    static constexpr size_t kIndexMaxRows = 65536;
    static constexpr size_t kIndexMaxPerClass = 4096;
    // Hit list cap: Engine::kValueSearchHitCap (500).

    mutable std::mutex mutex{};

    std::atomic<uint64_t> searchId{0};
    bool                  inProgress = false;
    std::shared_ptr<const std::vector<MethodSearchRow>> hits =
        std::make_shared<const std::vector<MethodSearchRow>>();
    uint64_t hitsGeneration = 0;
    std::shared_ptr<const std::string> statusMessage = std::make_shared<const std::string>();
    int selectedHitIndex = -1;

    std::shared_ptr<const std::vector<MethodSearchRow>> index =
        std::make_shared<const std::vector<MethodSearchRow>>();
    uint64_t    indexGeneration = 0;
    const void* indexCachePtr = nullptr;
    size_t      indexCacheCount = 0;
    void*       indexImage = nullptr;
    bool        hasIndex = false;
    bool        indexTruncated = false;
    bool        hitsTruncated = false;

    struct Snapshot {
        uint64_t searchId = 0;
        bool inProgress = false;
        std::shared_ptr<const std::vector<MethodSearchRow>> hits{};
        uint64_t hitsGeneration = 0;
        std::shared_ptr<const std::string> statusMessage{};
        int selectedHitIndex = -1;
        std::shared_ptr<const std::vector<MethodSearchRow>> index{};
        uint64_t    indexGeneration = 0;
        const void* indexCachePtr = nullptr;
        size_t      indexCacheCount = 0;
        void*       indexImage = nullptr;
        bool        hasIndex = false;
        bool        indexTruncated = false;
        bool        hitsTruncated = false;
    };

    Snapshot TakeSnapshot() const {
        std::lock_guard<std::mutex> lock(mutex);
        Snapshot snap{};
        snap.searchId = searchId.load(std::memory_order_relaxed);
        snap.inProgress = inProgress;
        snap.hits = hits;
        snap.hitsGeneration = hitsGeneration;
        snap.statusMessage = statusMessage;
        snap.selectedHitIndex = selectedHitIndex;
        snap.index = index;
        snap.indexGeneration = indexGeneration;
        snap.indexCachePtr = indexCachePtr;
        snap.indexCacheCount = indexCacheCount;
        snap.indexImage = indexImage;
        snap.hasIndex = hasIndex;
        snap.indexTruncated = indexTruncated;
        snap.hitsTruncated = hitsTruncated;
        return snap;
    }

    void BeginRun(uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex);
        searchId.store(id, std::memory_order_relaxed);
        inProgress = true;
        hits = std::make_shared<const std::vector<MethodSearchRow>>();
        ++hitsGeneration;
        selectedHitIndex = -1;
        hitsTruncated = false;
        statusMessage = std::make_shared<const std::string>("Indexing...");
    }

    void Publish(uint64_t id,
                 std::vector<MethodSearchRow> indexRows,
                 std::vector<MethodSearchRow> hitRows,
                 const void* cachePtr,
                 size_t cacheCount,
                 void* image,
                 bool indexTrunc,
                 bool hitsTrunc) {
        std::lock_guard<std::mutex> lock(mutex);
        if (searchId.load(std::memory_order_relaxed) != id) {
            return;
        }
        index = std::make_shared<const std::vector<MethodSearchRow>>(std::move(indexRows));
        ++indexGeneration;
        hits = std::make_shared<const std::vector<MethodSearchRow>>(std::move(hitRows));
        ++hitsGeneration;
        indexCachePtr = cachePtr;
        indexCacheCount = cacheCount;
        indexImage = image;
        hasIndex = true;
        indexTruncated = indexTrunc;
        hitsTruncated = hitsTrunc;
        statusMessage = std::make_shared<const std::string>();
        inProgress = false;
        selectedHitIndex = hits->empty() ? -1 : 0;
    }

    void PublishHits(std::vector<MethodSearchRow> hitRows, bool hitsTrunc) {
        std::lock_guard<std::mutex> lock(mutex);
        hits = std::make_shared<const std::vector<MethodSearchRow>>(std::move(hitRows));
        ++hitsGeneration;
        hitsTruncated = hitsTrunc;
        selectedHitIndex = hits->empty() ? -1 : 0;
    }

    void Invalidate() {
        std::lock_guard<std::mutex> lock(mutex);
        hasIndex = false;
        indexTruncated = false;
        hitsTruncated = false;
        indexCachePtr = nullptr;
        indexCacheCount = 0;
        indexImage = nullptr;
        index = std::make_shared<const std::vector<MethodSearchRow>>();
        hits = std::make_shared<const std::vector<MethodSearchRow>>();
        ++indexGeneration;
        ++hitsGeneration;
        selectedHitIndex = -1;
        statusMessage = std::make_shared<const std::string>();
    }

    void MarkCancelled(uint64_t id) {
        std::lock_guard<std::mutex> lock(mutex);
        if (searchId.load(std::memory_order_relaxed) != id) {
            return;
        }
        if (!inProgress) {
            return;
        }
        inProgress = false;
        statusMessage = std::make_shared<const std::string>("Cancelled");
    }
};
} // namespace Gui::State

#endif // ENABLE_DUMPER
