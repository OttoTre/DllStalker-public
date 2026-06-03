#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/session_state.h"

#include "services/main_thread_dispatcher.h"

namespace Gui
{
// ====================================================================
// Async load entry points
//
// Pattern for every Start* method:
//   1. Move-assign an empty jthread into the worker member. That triggers
//      request_stop + join on whatever was previously there, so the previous
//      worker either already finished (and its write was published) OR sees
//      the stop request before its write block and bails out. Either way the
//      cache is in a known state when we resume.
//   2. Set the in-progress flag.
//   3. If we have nothing to load, clear the flag and return (no worker).
//   4. Spawn the new jthread, capturing the dumper / target by value. The
//      worker checks the stop_token before each write so a new Start*Load
//      issued while it is mid-flight cannot poison the cache.
// ====================================================================

void ControlPanelSessionState::StartImageLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef) {
    loaders.imageLoadThread = {};
    loaders.imageLoadInProgress.store(true);

    if (!dumperRef) {
        loaders.imageLoadInProgress.store(false);
        return;
    }

    loaders.imageLoadThread = std::jthread([this, dumperRef](std::stop_token stopToken) {
        Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
        auto loadedImages = dumperRef->GetLoadedImages();
        if (stopToken.stop_requested()) {
            loaders.imageLoadInProgress.store(false);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(imageCache.mutex);
            imageCache.data = std::move(loadedImages);
        }
        loaders.imageLoadInProgress.store(false);
    });
}

void ControlPanelSessionState::StartClassLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedImageSnapshot) {
    loaders.classLoadThread = {};
    loaders.classLoadInProgress.store(true);

    if (!dumperRef || !selectedImageSnapshot) {
        loaders.classLoadInProgress.store(false);
        return;
    }

    loaders.classLoadThread = std::jthread([this, dumperRef, selectedImageSnapshot](std::stop_token stopToken) {
        Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
        auto loadedClasses = dumperRef->GetRawClasses(selectedImageSnapshot);
        if (stopToken.stop_requested()) {
            loaders.classLoadInProgress.store(false);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(classCache.mutex);
            classCache.data = std::move(loadedClasses);
        }
        loaders.classLoadInProgress.store(false);
    });
}

void ControlPanelSessionState::StartInspectorLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot) {
    // The inspector and the static-instance search both write into
    // inspector.cache, so cancel any running instance scan as well.
    loaders.inspectorLoadThread = {};
    loaders.instanceSearchThread = {};
    loaders.inspectorLoadInProgress.store(true);

    if (!dumperRef || !selectedClassSnapshot) {
        loaders.inspectorLoadInProgress.store(false);
        return;
    }

    loaders.inspectorLoadThread = std::jthread([this, dumperRef, selectedClassSnapshot](std::stop_token stopToken) {
        Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
        try {
            InspectorCache loadedCache{};
            loadedCache.activeClassPtr = selectedClassSnapshot;
            loadedCache.methods = dumperRef->GetRawMethods(selectedClassSnapshot);
            if (stopToken.stop_requested()) {
                loaders.inspectorLoadInProgress.store(false);
                return;
            }
            loadedCache.fields = dumperRef->GetRawFields(selectedClassSnapshot, nullptr);
            // Fields were loaded with instance=nullptr (static-only). The
            // inspector_frame reconciler watches this field and triggers
            // StartFieldsLoad once Static/Live discovery auto-populates
            // activeInstancePtr.
            loadedCache.fieldsLoadedForInstance = nullptr;
            if (stopToken.stop_requested()) {
                loaders.inspectorLoadInProgress.store(false);
                return;
            }

            std::lock_guard<std::mutex> lock(inspector.mutex);
            inspector.cache = std::move(loadedCache);
            inspector.selectedInstanceIndex = -1;
            editBufferStore.buffers.clear();
            enumLiteralCache.Clear();
        }
        catch (...) {
            std::lock_guard<std::mutex> lock(inspector.mutex);
            inspector.cache = {};
            inspector.selectedInstanceIndex = -1;
            editBufferStore.buffers.clear();
            enumLiteralCache.Clear();
        }

        loaders.inspectorLoadInProgress.store(false);
    });
}

void ControlPanelSessionState::StartFieldsLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot) {
    loaders.fieldsLoadThread = {};
    loaders.fieldsLoadInProgress.store(true);

    if (!dumperRef || !selectedClassSnapshot) {
        loaders.fieldsLoadInProgress.store(false);
        return;
    }

    loaders.fieldsLoadThread = std::jthread([this, dumperRef, selectedClassSnapshot](std::stop_token stopToken) {
        Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
        try {
            void* activeInstanceSnapshot = nullptr;
            {
                std::lock_guard<std::mutex> lock(inspector.mutex);
                activeInstanceSnapshot = inspector.cache.activeInstancePtr;
            }

            auto loadedFields = dumperRef->GetRawFields(selectedClassSnapshot, activeInstanceSnapshot);
            if (stopToken.stop_requested()) {
                loaders.fieldsLoadInProgress.store(false);
                return;
            }

            std::lock_guard<std::mutex> lock(inspector.mutex);
            inspector.cache.activeClassPtr = selectedClassSnapshot;
            inspector.cache.fields = std::move(loadedFields);
            // Match the instance pointer we actually loaded against so the
            // inspector_frame reconciler treats this slice as up-to-date.
            inspector.cache.fieldsLoadedForInstance = activeInstanceSnapshot;
        }
        catch (...) {
            // Field reads can throw if the underlying instance is freed
            // mid-scan; swallow so the worker exits cleanly.
        }

        loaders.fieldsLoadInProgress.store(false);
    });
}

void ControlPanelSessionState::StartStaticInstanceSearch(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot) {
    loaders.instanceSearchThread = {};
    loaders.instanceSearchInProgress.store(true);

    if (!dumperRef || !selectedClassSnapshot) {
        loaders.instanceSearchInProgress.store(false);
        return;
    }

    loaders.instanceSearchThread = std::jthread([this, dumperRef, selectedClassSnapshot](std::stop_token stopToken) {
        Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
        std::vector<void*> candidates;
        try {
            candidates = dumperRef->FindStaticInstanceCandidates(selectedClassSnapshot);
        }
        catch (...) {
            candidates.clear();
        }

        if (stopToken.stop_requested()) {
            loaders.instanceSearchInProgress.store(false);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(inspector.mutex);
            inspector.rootInstanceCandidates = candidates;
            inspector.cache.instanceCandidates = std::move(candidates);
            inspector.cache.activeClassPtr = selectedClassSnapshot;
            inspector.cache.activeInstancePtr = inspector.cache.instanceCandidates.empty() ? nullptr : inspector.cache.instanceCandidates.front();
            inspector.selectedInstanceIndex = inspector.cache.instanceCandidates.empty() ? -1 : 0;
        }

        loaders.instanceSearchInProgress.store(false);
    });
}

void ControlPanelSessionState::StartInspectorLoadAtInstance(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* klass, void* instance) {
    // Same cancel-then-relaunch pattern as StartInspectorLoad. We also stop
    // the instance-search worker because both writers touch inspector.cache,
    // and we'll be replacing instanceCandidates ourselves.
    loaders.inspectorLoadThread = {};
    loaders.instanceSearchThread = {};
    loaders.inspectorLoadInProgress.store(true);

    if (!dumperRef || !klass || !instance) {
        loaders.inspectorLoadInProgress.store(false);
        return;
    }

    loaders.inspectorLoadThread = std::jthread([this, dumperRef, klass, instance](std::stop_token stopToken) {
        Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
        try {
            InspectorCache loadedCache{};
            loadedCache.activeClassPtr = klass;
            loadedCache.activeInstancePtr = instance;
            // Single-element candidate list keeps the existing Fields-tab
            // refresh path working: it reads activeInstancePtr from the
            // cache and feeds it into GetRawFields on every Auto refresh.
            loadedCache.instanceCandidates = { instance };

            loadedCache.methods = dumperRef->GetRawMethods(klass);
            if (stopToken.stop_requested()) {
                loaders.inspectorLoadInProgress.store(false);
                return;
            }
            loadedCache.fields = dumperRef->GetRawFields(klass, instance);
            // Fields are instance-aware -- mark them so the inspector_frame
            // reconciler doesn't re-fire StartFieldsLoad on the next tick.
            loadedCache.fieldsLoadedForInstance = instance;
            if (stopToken.stop_requested()) {
                loaders.inspectorLoadInProgress.store(false);
                return;
            }

            std::lock_guard<std::mutex> lock(inspector.mutex);
            inspector.cache = std::move(loadedCache);
            inspector.selectedInstanceIndex = 0;
            // Different instance => previously-typed edit values are no
            // longer valid (their target addresses changed).
            editBufferStore.buffers.clear();
            enumLiteralCache.Clear();
        }
        catch (...) {
            std::lock_guard<std::mutex> lock(inspector.mutex);
            inspector.cache = {};
            inspector.selectedInstanceIndex = -1;
            editBufferStore.buffers.clear();
            enumLiteralCache.Clear();
        }

        loaders.inspectorLoadInProgress.store(false);
    });
}

void ControlPanelSessionState::StartCollectionLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef,
                                                    const Engine::FieldInfo& sourceField,
                                                    void* ownerKlass,
                                                    void* ownerInstance) {
    // Same cancel-then-relaunch discipline as the other Start*Load methods,
    // and we cancel the instance-search worker as well because both writers
    // touch inspector.cache.
    loaders.inspectorLoadThread = {};
    loaders.instanceSearchThread = {};
    loaders.inspectorLoadInProgress.store(true);

    if (!dumperRef) {
        loaders.inspectorLoadInProgress.store(false);
        return;
    }

    // Capture the source field by value: jthread workers outlive the click
    // frame, and we want a stable copy of the address / type pair to feed
    // into GetCollectionView on auto-refresh ticks.
    loaders.inspectorLoadThread = std::jthread([this, dumperRef, sourceField,
                                                ownerKlass, ownerInstance](std::stop_token stopToken) {
        Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
        try {
            std::vector<Engine::FieldInfo> elementRows;
            try {
                elementRows = dumperRef->GetCollectionView(sourceField);
            }
            catch (...) {
                elementRows.clear();
            }

            if (stopToken.stop_requested()) {
                loaders.inspectorLoadInProgress.store(false);
                return;
            }

            std::lock_guard<std::mutex> lock(inspector.mutex);
            // Preserve activeClassPtr / activeInstancePtr from the parent
            // breadcrumb so the existing Fields-tab status bar still shows
            // the owning instance address and the per-row drill-ins on
            // reference elements still find their target. Methods are not
            // meaningful in a collection view, so clear them.
            inspector.cache.methods.clear();
            inspector.cache.fields = std::move(elementRows);
            inspector.cache.instanceCandidates.clear();
            // Publish owner context so the inspector_frame reconciler does
            // not see a class mismatch and fire StartInspectorLoad on the
            // next tick (which would replace the collection view with a
            // plain class inspector and lose the field rows).
            inspector.cache.activeClassPtr          = ownerKlass;
            inspector.cache.activeInstancePtr       = ownerInstance;
            inspector.cache.fieldsLoadedForInstance = ownerInstance;
            // Collection element addresses can change between refreshes (GC moves objects).
            // Clear address-keyed edit buffers to avoid reusing stale values on new rows.
            editBufferStore.buffers.clear();
            enumLiteralCache.Clear();
        }
        catch (...) {
            std::lock_guard<std::mutex> lock(inspector.mutex);
            inspector.cache.methods.clear();
            inspector.cache.fields.clear();
            inspector.cache.instanceCandidates.clear();
            editBufferStore.buffers.clear();
            enumLiteralCache.Clear();
        }

        loaders.inspectorLoadInProgress.store(false);
    });
}

void ControlPanelSessionState::StartLiveInstanceSearch(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot) {
    loaders.instanceSearchThread = {};
    loaders.instanceSearchInProgress.store(true);

    if (!dumperRef || !selectedClassSnapshot) {
        loaders.instanceSearchInProgress.store(false);
        return;
    }

    loaders.instanceSearchThread = std::jthread([this, dumperRef, selectedClassSnapshot](std::stop_token stopToken) {
        Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
        std::vector<void*> candidates;
        try {
            candidates = dumperRef->GetLiveInstances(selectedClassSnapshot);
        }
        catch (...) {
            candidates.clear();
        }

        if (stopToken.stop_requested()) {
            loaders.instanceSearchInProgress.store(false);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(inspector.mutex);
            inspector.rootInstanceCandidates = candidates;
            inspector.cache.instanceCandidates = std::move(candidates);
            inspector.cache.activeClassPtr = selectedClassSnapshot;
            inspector.cache.activeInstancePtr = inspector.cache.instanceCandidates.empty() ? nullptr : inspector.cache.instanceCandidates.front();
            inspector.selectedInstanceIndex = inspector.cache.instanceCandidates.empty() ? -1 : 0;
        }

        loaders.instanceSearchInProgress.store(false);
    });
}
} // namespace Gui

#endif // ENABLE_DUMPER
