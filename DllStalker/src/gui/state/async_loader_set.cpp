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
    imageLoadThread = {};
    imageLoadInProgress.store(true);

    if (!dumperRef) {
        imageLoadInProgress.store(false);
        return;
    }

    imageLoadThread = std::jthread([this, dumperRef](std::stop_token stopToken) {
        Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
        auto loadedImages = dumperRef->GetLoadedImages();
        if (stopToken.stop_requested()) {
            imageLoadInProgress.store(false);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(imageCacheMutex);
            uiImageCache = std::move(loadedImages);
        }
        imageLoadInProgress.store(false);
    });
}

void ControlPanelSessionState::StartClassLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedImageSnapshot) {
    classLoadThread = {};
    classLoadInProgress.store(true);

    if (!dumperRef || !selectedImageSnapshot) {
        classLoadInProgress.store(false);
        return;
    }

    classLoadThread = std::jthread([this, dumperRef, selectedImageSnapshot](std::stop_token stopToken) {
        Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
        auto loadedClasses = dumperRef->GetRawClasses(selectedImageSnapshot);
        if (stopToken.stop_requested()) {
            classLoadInProgress.store(false);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(classCacheMutex);
            uiClassCache = std::move(loadedClasses);
        }
        classLoadInProgress.store(false);
    });
}

void ControlPanelSessionState::StartInspectorLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot) {
    // The inspector and the static-instance search both write into
    // inspectorCache, so cancel any running instance scan as well.
    inspectorLoadThread = {};
    instanceSearchThread = {};
    inspectorLoadInProgress.store(true);

    if (!dumperRef || !selectedClassSnapshot) {
        inspectorLoadInProgress.store(false);
        return;
    }

    inspectorLoadThread = std::jthread([this, dumperRef, selectedClassSnapshot](std::stop_token stopToken) {
        Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
        try {
            InspectorCache loadedCache{};
            loadedCache.activeClassPtr = selectedClassSnapshot;
            loadedCache.methods = dumperRef->GetRawMethods(selectedClassSnapshot);
            if (stopToken.stop_requested()) {
                inspectorLoadInProgress.store(false);
                return;
            }
            loadedCache.fields = dumperRef->GetRawFields(selectedClassSnapshot, nullptr);
            // Fields were loaded with instance=nullptr (static-only). The
            // inspector_frame reconciler watches this field and triggers
            // StartFieldsLoad once Static/Live discovery auto-populates
            // activeInstancePtr.
            loadedCache.fieldsLoadedForInstance = nullptr;
            if (stopToken.stop_requested()) {
                inspectorLoadInProgress.store(false);
                return;
            }

            std::lock_guard<std::mutex> lock(inspectorCacheMutex);
            inspectorCache = std::move(loadedCache);
            selectedInstanceIndex = -1;
            editBuffers.clear();
        }
        catch (...) {
            std::lock_guard<std::mutex> lock(inspectorCacheMutex);
            inspectorCache = {};
            selectedInstanceIndex = -1;
            editBuffers.clear();
        }

        inspectorLoadInProgress.store(false);
    });
}

void ControlPanelSessionState::StartFieldsLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot) {
    fieldsLoadThread = {};
    fieldsLoadInProgress.store(true);

    if (!dumperRef || !selectedClassSnapshot) {
        fieldsLoadInProgress.store(false);
        return;
    }

    fieldsLoadThread = std::jthread([this, dumperRef, selectedClassSnapshot](std::stop_token stopToken) {
        Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
        try {
            void* activeInstanceSnapshot = nullptr;
            {
                std::lock_guard<std::mutex> lock(inspectorCacheMutex);
                activeInstanceSnapshot = inspectorCache.activeInstancePtr;
            }

            auto loadedFields = dumperRef->GetRawFields(selectedClassSnapshot, activeInstanceSnapshot);
            if (stopToken.stop_requested()) {
                fieldsLoadInProgress.store(false);
                return;
            }

            std::lock_guard<std::mutex> lock(inspectorCacheMutex);
            inspectorCache.activeClassPtr = selectedClassSnapshot;
            inspectorCache.fields = std::move(loadedFields);
            // Match the instance pointer we actually loaded against so the
            // inspector_frame reconciler treats this slice as up-to-date.
            inspectorCache.fieldsLoadedForInstance = activeInstanceSnapshot;
        }
        catch (...) {
            // Field reads can throw if the underlying instance is freed
            // mid-scan; swallow so the worker exits cleanly.
        }

        fieldsLoadInProgress.store(false);
    });
}

void ControlPanelSessionState::StartStaticInstanceSearch(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot) {
    instanceSearchThread = {};
    instanceSearchInProgress.store(true);

    if (!dumperRef || !selectedClassSnapshot) {
        instanceSearchInProgress.store(false);
        return;
    }

    instanceSearchThread = std::jthread([this, dumperRef, selectedClassSnapshot](std::stop_token stopToken) {
        Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
        std::vector<void*> candidates;
        try {
            candidates = dumperRef->FindStaticInstanceCandidates(selectedClassSnapshot);
        }
        catch (...) {
            candidates.clear();
        }

        if (stopToken.stop_requested()) {
            instanceSearchInProgress.store(false);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(inspectorCacheMutex);
            inspector.rootInstanceCandidates = candidates;
            inspectorCache.instanceCandidates = std::move(candidates);
            inspectorCache.activeClassPtr = selectedClassSnapshot;
            inspectorCache.activeInstancePtr = inspectorCache.instanceCandidates.empty() ? nullptr : inspectorCache.instanceCandidates.front();
            selectedInstanceIndex = inspectorCache.instanceCandidates.empty() ? -1 : 0;
        }

        instanceSearchInProgress.store(false);
    });
}

void ControlPanelSessionState::StartInspectorLoadAtInstance(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* klass, void* instance) {
    // Same cancel-then-relaunch pattern as StartInspectorLoad. We also stop
    // the instance-search worker because both writers touch inspectorCache,
    // and we'll be replacing instanceCandidates ourselves.
    inspectorLoadThread = {};
    instanceSearchThread = {};
    inspectorLoadInProgress.store(true);

    if (!dumperRef || !klass || !instance) {
        inspectorLoadInProgress.store(false);
        return;
    }

    inspectorLoadThread = std::jthread([this, dumperRef, klass, instance](std::stop_token stopToken) {
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
                inspectorLoadInProgress.store(false);
                return;
            }
            loadedCache.fields = dumperRef->GetRawFields(klass, instance);
            // Fields are instance-aware -- mark them so the inspector_frame
            // reconciler doesn't re-fire StartFieldsLoad on the next tick.
            loadedCache.fieldsLoadedForInstance = instance;
            if (stopToken.stop_requested()) {
                inspectorLoadInProgress.store(false);
                return;
            }

            std::lock_guard<std::mutex> lock(inspectorCacheMutex);
            inspectorCache = std::move(loadedCache);
            selectedInstanceIndex = 0;
            // Different instance => previously-typed edit values are no
            // longer valid (their target addresses changed).
            editBuffers.clear();
        }
        catch (...) {
            std::lock_guard<std::mutex> lock(inspectorCacheMutex);
            inspectorCache = {};
            selectedInstanceIndex = -1;
            editBuffers.clear();
        }

        inspectorLoadInProgress.store(false);
    });
}

void ControlPanelSessionState::StartCollectionLoad(const std::shared_ptr<Engine::UnityDumper>& dumperRef,
                                                    const Engine::FieldInfo& sourceField) {
    // Same cancel-then-relaunch discipline as the other Start*Load methods,
    // and we cancel the instance-search worker as well because both writers
    // touch inspectorCache.
    inspectorLoadThread = {};
    instanceSearchThread = {};
    inspectorLoadInProgress.store(true);

    if (!dumperRef) {
        inspectorLoadInProgress.store(false);
        return;
    }

    // Capture the source field by value: jthread workers outlive the click
    // frame, and we want a stable copy of the address / type pair to feed
    // into GetCollectionView on auto-refresh ticks.
    inspectorLoadThread = std::jthread([this, dumperRef, sourceField](std::stop_token stopToken) {
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
                inspectorLoadInProgress.store(false);
                return;
            }

            std::lock_guard<std::mutex> lock(inspectorCacheMutex);
            // Preserve activeClassPtr / activeInstancePtr from the parent
            // breadcrumb so the existing Fields-tab status bar still shows
            // the owning instance address and the per-row drill-ins on
            // reference elements still find their target. Methods are not
            // meaningful in a collection view, so clear them.
            inspectorCache.methods.clear();
            inspectorCache.fields = std::move(elementRows);
            inspectorCache.instanceCandidates.clear();
            // Collection element addresses can change between refreshes (GC moves objects).
            // Clear address-keyed edit buffers to avoid reusing stale values on new rows.
            editBuffers.clear();
        }
        catch (...) {
            std::lock_guard<std::mutex> lock(inspectorCacheMutex);
            inspectorCache.methods.clear();
            inspectorCache.fields.clear();
            inspectorCache.instanceCandidates.clear();
            editBuffers.clear();
        }

        inspectorLoadInProgress.store(false);
    });
}

void ControlPanelSessionState::StartLiveInstanceSearch(const std::shared_ptr<Engine::UnityDumper>& dumperRef, void* selectedClassSnapshot) {
    instanceSearchThread = {};
    instanceSearchInProgress.store(true);

    if (!dumperRef || !selectedClassSnapshot) {
        instanceSearchInProgress.store(false);
        return;
    }

    instanceSearchThread = std::jthread([this, dumperRef, selectedClassSnapshot](std::stop_token stopToken) {
        Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
        std::vector<void*> candidates;
        try {
            candidates = dumperRef->GetLiveInstances(selectedClassSnapshot);
        }
        catch (...) {
            candidates.clear();
        }

        if (stopToken.stop_requested()) {
            instanceSearchInProgress.store(false);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(inspectorCacheMutex);
            inspector.rootInstanceCandidates = candidates;
            inspectorCache.instanceCandidates = std::move(candidates);
            inspectorCache.activeClassPtr = selectedClassSnapshot;
            inspectorCache.activeInstancePtr = inspectorCache.instanceCandidates.empty() ? nullptr : inspectorCache.instanceCandidates.front();
            selectedInstanceIndex = inspectorCache.instanceCandidates.empty() ? -1 : 0;
        }

        instanceSearchInProgress.store(false);
    });
}
} // namespace Gui

#endif // ENABLE_DUMPER
