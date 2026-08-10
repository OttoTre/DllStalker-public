#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/session_state.h"

#include "dumper/value_search/value_search_scanner.h"
#include "services/main_thread_dispatcher.h"
#include "types/dumper_types.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Gui
{
namespace
{
std::string FormatClassLabel(const Engine::ClassInfo& cl) {
    return cl.ns.empty() ? cl.name : (cl.ns + "::" + cl.name);
}

std::string ResolveSelectedImageName(ControlPanelSessionState& state) {
    if (!state.selectedImage) {
        return {};
    }
    for (const auto& img : *state.GetImageCacheSnapshot()) {
        if (img.imagePtr == state.selectedImage) {
            return img.name.empty() ? std::string("<image>") : img.name;
        }
    }
    return "<image>";
}

Engine::ValueSearchParams BuildParamsFromUi(ControlPanelSessionState& state, bool drill) {
    Engine::ValueSearchParams params{};
    params.klass = state.selectedClass;
    params.className = ResolveSelectedImageName(state);
    params.nameNeedle = drill ? std::string{} : std::string(state.valueSearchNameBuffer);
    params.valueNeedle = state.valueSearchValueBuffer;
    params.chipNumber = state.valueSearch.chipNumber;
    params.chipString = state.valueSearch.chipString;
    params.chipBool = true;
    params.chipEnum = true;
    params.chipPtr = state.valueSearch.chipPtr;
    params.chipDeep = state.valueSearch.chipDeep;
    params.nameMatchStrict = state.valueSearch.nameMatchStrict;
    params.valueMatchStrict = state.valueSearch.valueMatchStrict;
    params.drillMode = drill;
    if (drill) {
        std::lock_guard<std::mutex> lock(state.valueSearch.mutex);
        params.baselineHits = state.valueSearch.hits;
    }
    return params;
}

std::string FormatSearchStatus(const Engine::ValueSearchScanResult& result, bool drill) {
    char buf[256] = {};
    const char* verb = drill ? "Drill" : "Search";
    if (!drill && result.classesSkippedBySchema > 0 && result.hits.empty()
        && result.instancesScanned == 0) {
        std::snprintf(buf, sizeof(buf),
                      "%s: no fields match name/type chips (%zu classes skipped)",
                      verb, result.classesSkippedBySchema);
        return buf;
    }
    if (result.truncReason == Engine::ValueSearchTruncReason::HitCap) {
        std::snprintf(buf, sizeof(buf),
                      "%s: %zu hits (Hit limit %zu) — %zu instances",
                      verb, result.hits.size(), Engine::kValueSearchHitCap,
                      result.instancesScanned);
    }
    else if (!drill && result.classesSkippedBySchema > 0) {
        std::snprintf(buf, sizeof(buf),
                      "%s: %zu hits — %zu instances (%zu classes skipped)",
                      verb, result.hits.size(), result.instancesScanned,
                      result.classesSkippedBySchema);
    }
    else {
        std::snprintf(buf, sizeof(buf),
                      "%s: %zu hits — %zu instances",
                      verb, result.hits.size(), result.instancesScanned);
    }
    return buf;
}

std::vector<void*> CollectImageClassInstances(Engine::UnityDumper& dumper, void* klass) {
    std::vector<void*> candidates;
    try {
        candidates = dumper.FindStaticInstanceCandidates(klass);
    }
    catch (...) {
        candidates.clear();
    }
    // Live FindObjectsOfType via MainThreadDispatcher when static set empty.
    if (candidates.empty()) {
        try {
            candidates = dumper.GetLiveInstances(klass);
        }
        catch (...) {
            candidates.clear();
        }
    }
    return candidates;
}

Engine::ValueSearchScanResult RunImageScopeSearch(
    Engine::UnityDumper& dumper,
    Engine::ValueSearchParams params,
    std::vector<Engine::ClassInfo> classes,
    std::stop_token stopToken) {
    Engine::ValueSearchScanResult merged{};
    auto& schemas = dumper.ValueSearchSchemas();

    for (const auto& cl : classes) {
        if (stopToken.stop_requested()) {
            break;
        }
        if (!cl.klassPtr) {
            continue;
        }

        params.klass = cl.klassPtr;
        params.className = FormatClassLabel(cl);

        // Schema gate before static/live discovery.
        const Engine::Dumper::ValueSearchSchemaLookup lookup = schemas.GetOrBuild(dumper, cl.klassPtr);
        if (lookup.usable && lookup.schema
            && !Engine::Dumper::ClassSchemaMayMatch(*lookup.schema, params)) {
            ++merged.classesSkippedBySchema;
            continue;
        }

        std::vector<void*> candidates = CollectImageClassInstances(dumper, cl.klassPtr);

        Engine::ValueSearchScanResult part{};
        try {
            part = Engine::Dumper::RunValueSearch(
                dumper, params, candidates, stopToken, &schemas,
                (lookup.usable && lookup.schema) ? lookup.schema : decltype(lookup.schema){});
        }
        catch (...) {
            part = {};
        }

        merged.classesSkippedBySchema += part.classesSkippedBySchema;
        merged.instancesScanned += part.instancesScanned;
        merged.fieldsVisited += part.fieldsVisited;

        for (auto& hit : part.hits) {
            if (merged.hits.size() >= Engine::kValueSearchHitCap) {
                merged.truncReason = Engine::ValueSearchTruncReason::HitCap;
                return merged;
            }
            merged.hits.push_back(std::move(hit));
        }
        if (merged.hits.size() >= Engine::kValueSearchHitCap
            || part.truncReason == Engine::ValueSearchTruncReason::HitCap) {
            merged.truncReason = Engine::ValueSearchTruncReason::HitCap;
            return merged;
        }
    }
    return merged;
}
} // namespace

void ControlPanelSessionState::CancelValueSearch() {
    loaders.valueSearchThread = {};
    loaders.valueSearchInProgress.store(false);
    const uint64_t id = valueSearch.searchId.load(std::memory_order_relaxed);
    valueSearch.MarkCancelled(id);
}

void ControlPanelSessionState::StartValueSearch() {
    CancelValueSearch();

    auto params = BuildParamsFromUi(*this, /*drill=*/false);

    if (!dumper) {
        const uint64_t id = valueSearch.searchId.fetch_add(1) + 1;
        valueSearch.BeginRun(id, false);
        valueSearch.Publish(id, {}, "Dumper not ready");
        return;
    }
    if (!selectedImage) {
        const uint64_t id = valueSearch.searchId.fetch_add(1) + 1;
        valueSearch.BeginRun(id, false);
        valueSearch.Publish(id, {}, "Select an image first");
        return;
    }
    params.className = ResolveSelectedImageName(*this);

    if (params.nameNeedle.empty() && params.valueNeedle.empty()) {
        const uint64_t id = valueSearch.searchId.fetch_add(1) + 1;
        valueSearch.BeginRun(id, false);
        valueSearch.Publish(id, {}, "Enter a name and/or value");
        return;
    }

    const uint64_t id = valueSearch.searchId.fetch_add(1) + 1;
    valueSearch.BeginRun(id, false);
    loaders.valueSearchInProgress.store(true);

    auto dumperRef = dumper;
    void* const imagePtr = selectedImage;
    loaders.valueSearchThread = std::jthread(
        [this, dumperRef, imagePtr, params = std::move(params), id](
            std::stop_token stopToken) mutable {
            Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();

            std::vector<Engine::ClassInfo> classes;
            if (dumperRef && imagePtr) {
                try {
                    classes = dumperRef->GetRawClasses(imagePtr);
                }
                catch (...) {
                    classes.clear();
                }
            }

            Engine::ValueSearchScanResult result{};
            if (!stopToken.stop_requested() && dumperRef) {
                try {
                    result = RunImageScopeSearch(*dumperRef, params, std::move(classes),
                                                 stopToken);
                }
                catch (...) {
                    result = {};
                }
            }

            if (stopToken.stop_requested()) {
                loaders.valueSearchInProgress.store(false);
                valueSearch.MarkCancelled(id);
                return;
            }

            valueSearch.Publish(id, std::move(result), FormatSearchStatus(result, false));
            loaders.valueSearchInProgress.store(false);
        });
}

void ControlPanelSessionState::StartValueDrill() {
    CancelValueSearch();

    if (!dumper) {
        return;
    }

    auto params = BuildParamsFromUi(*this, /*drill=*/true);
    if (params.valueNeedle.empty()) {
        const uint64_t id = valueSearch.searchId.fetch_add(1) + 1;
        valueSearch.BeginRun(id, true, params.baselineHits);
        valueSearch.Publish(id, {}, "Drill requires a value");
        return;
    }
    if (!params.baselineHits || params.baselineHits->empty()) {
        const uint64_t id = valueSearch.searchId.fetch_add(1) + 1;
        valueSearch.BeginRun(id, true, params.baselineHits);
        valueSearch.Publish(id, {}, "No baseline hits to drill");
        return;
    }

    const uint64_t id = valueSearch.searchId.fetch_add(1) + 1;
    valueSearch.BeginRun(id, true, params.baselineHits);
    loaders.valueSearchInProgress.store(true);

    auto dumperRef = dumper;

    loaders.valueSearchThread = std::jthread(
        [this, dumperRef, params = std::move(params), id](std::stop_token stopToken) {
            Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
            Engine::ValueSearchScanResult result{};
            if (!stopToken.stop_requested() && dumperRef) {
                try {
                    result = Engine::Dumper::RunValueSearch(*dumperRef, params, {}, stopToken);
                }
                catch (...) {
                    result = {};
                }
            }

            if (stopToken.stop_requested()) {
                loaders.valueSearchInProgress.store(false);
                valueSearch.MarkCancelled(id);
                return;
            }

            valueSearch.Publish(id, std::move(result), FormatSearchStatus(result, true));
            loaders.valueSearchInProgress.store(false);
        });
}
} // namespace Gui

#endif // ENABLE_DUMPER
