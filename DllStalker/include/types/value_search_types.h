#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Engine
{
struct ValueSearchHit {
    void*       klass         = nullptr;
    void*       instance      = nullptr;
    std::string className{};
    std::string fieldName{};
    std::string typeName{};
    std::string valueDisplay{};
    uintptr_t   valueAddress  = 0;
    bool        isEnum        = false;
};

enum class ValueSearchTruncReason {
    None = 0,
    HitCap = 1,
};

struct ValueSearchParams {
    void*       klass = nullptr;
    std::string className{};
    std::string nameNeedle{};
    std::string valueNeedle{};
    bool        chipNumber = true;
    bool        chipString = true;
    bool        chipBool   = true;  // always on (no UI chip)
    bool        chipEnum   = true;  // always on (no UI chip)
    bool        chipPtr    = true;
    bool        chipDeep   = false; // interiors + Follow + Array/List expand
    bool        nameMatchStrict  = false; // ~ fuzzy / = strict
    bool        valueMatchStrict = true;
    bool        drillMode  = false;
    std::shared_ptr<const std::vector<ValueSearchHit>> baselineHits{};
};

struct ValueSearchScanResult {
    std::vector<ValueSearchHit> hits{};
    ValueSearchTruncReason      truncReason = ValueSearchTruncReason::None;
    size_t                      instancesScanned = 0;
    size_t                      fieldsVisited    = 0;
    size_t                      classesSkippedBySchema = 0;
};

constexpr size_t kValueSearchHitCap = 500;
constexpr size_t kValueSearchMaxCollectionElements = 64;
constexpr size_t kValueSearchMaxElementInteriorFields = 32;
constexpr size_t kValueSearchMaxPtrFieldsFollowed = 16;
} // namespace Engine

#endif // ENABLE_DUMPER
