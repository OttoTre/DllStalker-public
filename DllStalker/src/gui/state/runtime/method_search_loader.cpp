#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/session_state.h"

#include "gui/infra/search_filter.h"
#include "services/main_thread_dispatcher.h"

#include <string>
#include <vector>

namespace Gui
{
namespace
{
std::string ClassDisplay(const State::MethodSearchRow& row) {
    return row.classNs.empty() ? row.className : (row.classNs + "::" + row.className);
}

bool RowMatches(const State::MethodSearchRow& row, const std::string& needle) {
    return Gui::Infra::SearchFilter::FuzzyMatch(row.methodName, needle)
        || Gui::Infra::SearchFilter::FuzzyMatch(ClassDisplay(row), needle);
}

std::vector<State::MethodSearchRow> FilterIndex(
    const std::vector<State::MethodSearchRow>& index,
    const std::string& needle,
    bool& hitsTruncated) {
    hitsTruncated = false;
    std::vector<State::MethodSearchRow> hits;
    const size_t cap = Engine::kValueSearchHitCap;
    hits.reserve(index.size() < cap ? index.size() : cap);
    for (const auto& row : index) {
        if (!RowMatches(row, needle)) {
            continue;
        }
        if (hits.size() >= cap) {
            hitsTruncated = true;
            break;
        }
        hits.push_back(row);
    }
    return hits;
}

State::MethodSearchRow StampRow(const Engine::ClassInfo& cl, Engine::MethodNameRow&& nameRow) {
    State::MethodSearchRow row{};
    row.klassPtr = cl.klassPtr;
    row.classNs = cl.ns;
    row.className = cl.name;
    row.methodName = std::move(nameRow.name);
    row.isStatic = nameRow.isStatic;
    row.paramCount = nameRow.paramCount;
    row.paramCountKnown = nameRow.paramCountKnown;
    return row;
}
} // namespace

void ControlPanelSessionState::CancelImageMethodIndex() {
    loaders.methodIndexThread = {};
    loaders.methodIndexInProgress.store(false);
    const uint64_t id = methodSearch.searchId.load(std::memory_order_relaxed);
    methodSearch.MarkCancelled(id);
}

void ControlPanelSessionState::StartImageMethodIndex() {
    CancelImageMethodIndex();

    if (!selectedImage) {
        methodSearch.Invalidate();
        return;
    }

    const auto classSnap = GetClassCacheSnapshot();
    void* const imagePtr = selectedImage;
    const std::string needle = Gui::Infra::SearchFilter::ToLowercase(methodSearchBuffer);

    const auto existing = methodSearch.TakeSnapshot();
    const bool identityMatches = existing.hasIndex
        && existing.indexCachePtr == classSnap.get()
        && existing.indexCacheCount == classSnap->size()
        && existing.indexImage == imagePtr;

    if (identityMatches) {
        static const std::vector<State::MethodSearchRow> kEmpty{};
        const auto& indexRows = existing.index ? *existing.index : kEmpty;
        bool hitsTrunc = false;
        auto hits = FilterIndex(indexRows, needle, hitsTrunc);
        methodSearch.PublishHits(std::move(hits), hitsTrunc);
        return;
    }

    if (!dumper) {
        return;
    }

    const uint64_t id = methodSearch.searchId.fetch_add(1) + 1;
    methodSearch.BeginRun(id);
    loaders.methodIndexInProgress.store(true);

    auto dumperRef = dumper;
    loaders.methodIndexThread = std::jthread(
        [this, dumperRef, classSnap, imagePtr, needle, id](std::stop_token stopToken) {
            Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();

            std::vector<State::MethodSearchRow> indexRows;
            bool indexTrunc = false;
            const size_t rowCap = State::MethodSearchModel::kIndexMaxRows;
            const size_t perClass = State::MethodSearchModel::kIndexMaxPerClass;

            if (dumperRef && classSnap) {
                for (const auto& cl : *classSnap) {
                    if (stopToken.stop_requested()) {
                        break;
                    }
                    if (!cl.klassPtr) {
                        continue;
                    }

                    std::vector<Engine::MethodNameRow> names;
                    bool classTrunc = false;
                    try {
                        names = dumperRef->EnumerateMethodNames(
                            cl.klassPtr, perClass, &classTrunc);
                    }
                    catch (...) {
                        names.clear();
                    }
                    if (classTrunc) {
                        indexTrunc = true;
                    }

                    for (auto& nameRow : names) {
                        if (indexRows.size() >= rowCap) {
                            indexTrunc = true;
                            break;
                        }
                        indexRows.push_back(StampRow(cl, std::move(nameRow)));
                    }
                    if (indexRows.size() >= rowCap) {
                        break;
                    }
                }
            }

            if (stopToken.stop_requested()) {
                loaders.methodIndexInProgress.store(false);
                methodSearch.MarkCancelled(id);
                return;
            }

            bool hitsTrunc = false;
            auto hits = FilterIndex(indexRows, needle, hitsTrunc);
            methodSearch.Publish(id, std::move(indexRows), std::move(hits),
                                 classSnap.get(), classSnap ? classSnap->size() : 0,
                                 imagePtr, indexTrunc, hitsTrunc);
            loaders.methodIndexInProgress.store(false);
        });
}
} // namespace Gui

#endif // ENABLE_DUMPER
