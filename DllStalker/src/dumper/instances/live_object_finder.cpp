#include "pch.h"

#ifdef ENABLE_DUMPER

#include "dumper/instances/live_object_finder.h"

#include "services/main_thread_dispatcher.h"
#include "unity_resolver.h"

#include <chrono>
#include <future>
#include <string>

#include "types/memory_guard.h"
#include "types/unity_array_layout.h"

namespace Engine::Dumper
{
namespace
{
static_assert(sizeof(void*) == 8, "GetLiveInstances assumes the x64 Unity array header layout");

constexpr size_t kMaxFindObjectsResultLength = 1'000'000;
constexpr auto kLiveFindDispatchTimeout = std::chrono::seconds(10);

// Mode values match LiveObjectFinder::LiveFindMode (kept private on the class).
constexpr int kLiveFindModeType1 = 0;
constexpr int kLiveFindModeType2 = 1;
constexpr int kLiveFindModeByType3 = 2;

void* GetSystemTypeForClassOnResolver(UnityResolver& resolver, void* klass) {
    if (!klass || !resolver.module.exports.fnClassGetType) return nullptr;

    resolver.module.EnsureThreadAttached();

    void* engineType = resolver.module.exports.fnClassGetType(klass);
    if (!engineType) return nullptr;

    if (resolver.module.isIL2CPP) {
        if (!resolver.module.exports.fnTypeGetObject) {
            return nullptr;
        }
        return resolver.module.exports.fnTypeGetObject(engineType);
    }

    if (!resolver.module.exports.fnMonoTypeGetObject || !resolver.module.domain) {
        return nullptr;
    }
    return resolver.module.exports.fnMonoTypeGetObject(resolver.module.domain, engineType);
}

std::vector<void*> InvokeFindObjectsAndCollect(UnityResolver& resolver,
                                               void* findMethod,
                                               int mode,
                                               void* systemType) {
    std::vector<void*> instances;
    if (!findMethod || !systemType) {
        return instances;
    }

    void* arrayResult = nullptr;
    void* exc         = nullptr;
    bool  invokedOk   = false;

    if (mode == kLiveFindModeType1) {
        void* params[1] = { systemType };
        invokedOk = resolver.invoker.InvokeWithSEH(findMethod, nullptr, params, &exc, arrayResult);
    }
    else if (mode == kLiveFindModeType2) {
        bool  inactive  = false;
        void* params[2] = { systemType, &inactive };
        invokedOk = resolver.invoker.InvokeWithSEH(findMethod, nullptr, params, &exc, arrayResult);
    }
    else {
        int   inactive  = 0;
        int   sortMode  = 0;
        void* params[3] = { systemType, &inactive, &sortMode };
        invokedOk = resolver.invoker.InvokeWithSEH(findMethod, nullptr, params, &exc, arrayResult);
    }

    if (!invokedOk || exc || !arrayResult) {
        return instances;
    }

    const uintptr_t arrayBase = reinterpret_cast<uintptr_t>(arrayResult);
    size_t arraySize = 0;
    if (!Memory::TryReadValue(arrayBase + Engine::UnityArrayLayout::LengthOffset, arraySize)) {
        return instances;
    }
    if (arraySize == 0 || arraySize > kMaxFindObjectsResultLength) {
        return instances;
    }

    const uintptr_t elementsBase = arrayBase + Engine::UnityArrayLayout::ElementsOffset;
    if (!Memory::IsReadablePointer(reinterpret_cast<void*>(elementsBase),
                                   arraySize * sizeof(void*))) {
        return instances;
    }

    instances.reserve(arraySize);
    for (size_t i = 0; i < arraySize; ++i) {
        void* item = nullptr;
        if (!Memory::TryReadValue(elementsBase + i * sizeof(void*), item) || !item) {
            continue;
        }
        instances.push_back(item);
    }

    return instances;
}

std::vector<void*> FindLiveInstancesOnMain(UnityResolver& resolver,
                                           void* klass,
                                           void* findMethod,
                                           int mode) {
    void* systemType = GetSystemTypeForClassOnResolver(resolver, klass);
    if (!systemType) {
        return {};
    }
    return InvokeFindObjectsAndCollect(resolver, findMethod, mode, systemType);
}
} // namespace

LiveObjectFinder::LiveObjectFinder(UnityResolver& resolver)
    : m_resolver(resolver)
{
}

bool LiveObjectFinder::TryResolveFindApiFromImage(void* image) {
    if (!image || !m_resolver.module.exports.fnGetClass || !m_resolver.module.exports.fnGetMethod) {
        return false;
    }

    void* unityObjectClass = m_resolver.module.exports.fnGetClass(image, "UnityEngine", "Object");
    if (!unityObjectClass) {
        return false;
    }

    struct Probe { const char* name; int args; LiveFindMode mode; };
    constexpr Probe probes[] = {
        { "FindObjectsByType", 3, LiveFindMode::ByType3 },
        { "FindObjectsOfType", 2, LiveFindMode::Type2   },
        { "FindObjectsOfType", 1, LiveFindMode::Type1   },
    };

    for (const auto& p : probes) {
        if (void* method = m_resolver.module.exports.fnGetMethod(unityObjectClass, p.name, p.args)) {
            m_cachedFindMethod  = method;
            m_cachedFindMode    = p.mode;
            m_liveApiCacheReady.store(true, std::memory_order_release);
            return true;
        }
    }
    return false;
}

bool LiveObjectFinder::EnsureLiveInstanceApiCached() {
    if (m_liveApiCacheReady.load(std::memory_order_acquire)) {
        return true;
    }

    std::lock_guard<std::mutex> lock(m_liveApiCacheMutex);
    if (m_liveApiCacheReady.load(std::memory_order_relaxed)) {
        return true;
    }

    // Prefer CoreModule; if the image is missing *or* Object lookup fails,
    // retry against UnityEngine. Failures are not sticky — next call retries.
    static const char* kImageCandidates[] = {
        "UnityEngine.CoreModule",
        "UnityEngine",
    };

    for (const char* imageName : kImageCandidates) {
        // Post-boot: single-pass exact (bare + .dll), then substring.
        // Avoid FindImageExact's long retry before falling back.
        constexpr int kOnce = 1;
        void* image = m_resolver.FindImageExact(imageName, kOnce);
        if (!image) {
            const std::string withDll = std::string(imageName) + ".dll";
            image = m_resolver.FindImageExact(withDll.c_str(), kOnce);
        }
        if (!image) {
            image = m_resolver.FindImage(imageName, kOnce);
        }
        if (!image) {
            continue;
        }
        if (TryResolveFindApiFromImage(image)) {
            return true;
        }
    }

    return false;
}

std::vector<void*> LiveObjectFinder::GetLiveInstances(void* klass) {
    std::vector<void*> instances;
    if (!klass || !m_resolver.module.exports.fnRuntimeInvoke) {
        return instances;
    }

    m_resolver.module.EnsureThreadAttached();

    if (!EnsureLiveInstanceApiCached()) {
        return instances;
    }

    void* const findMethod = m_cachedFindMethod;
    const int mode = static_cast<int>(m_cachedFindMode);
    if (!findMethod) {
        return instances;
    }

    if (Services::MainThreadDispatcher::IsOnMainThread()) {
        return FindLiveInstancesOnMain(m_resolver, klass, findMethod, mode);
    }

    if (!Services::MainThreadDispatcher::IsDispatchAvailable()
        || !Services::MainThreadDispatcher::IsMainThreadCaptured()) {
        return instances;
    }

    auto promise = std::make_shared<std::promise<std::vector<void*>>>();
    std::future<std::vector<void*>> future = promise->get_future();

    // Capture Unity singleton — never LiveObjectFinder*.
    UnityResolver* const resolver = &Unity;
    const bool enqueued = Services::MainThreadDispatcher::TryEnqueueNoDrop(
        [resolver, klass, findMethod, mode, promise]() {
            try {
                promise->set_value(FindLiveInstancesOnMain(*resolver, klass, findMethod, mode));
            }
            catch (...) {
                try {
                    promise->set_value({});
                }
                catch (...) {
                }
            }
        });

    if (!enqueued) {
        return instances;
    }

    if (future.wait_for(kLiveFindDispatchTimeout) != std::future_status::ready) {
        return instances;
    }

    try {
        return future.get();
    }
    catch (...) {
        return instances;
    }
}
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
