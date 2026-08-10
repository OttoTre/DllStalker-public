#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <atomic>
#include <mutex>
#include <vector>

#include "unity_resolver.h"

namespace Engine::Dumper
{
// Wraps UnityEngine.Object::FindObjectsOfType{,ByType} so callers can
// retrieve every live MonoBehaviour-derived instance of a class without
// caring about which signature variant the target Unity version exposes.
//
// Owns a retryable cache of the FindObjectsOfType method handle (mutex;
// failures do not permanently disable live discovery). Dumper-only export
// bootstrap lives on UnityDumper (InitDumperExports), not here.
class LiveObjectFinder
{
public:
    explicit LiveObjectFinder(UnityResolver& resolver);

    // Live snapshot of every loaded instance of `klass`. Empty on any
    // engine-side error (the caller treats that as "not found").
    // FindObjects runtime_invoke (+ System.Type build) always runs on the
    // captured Unity main thread via MainThreadDispatcher (inline when
    // already on main). MTD jobs never capture this finder.
    std::vector<void*> GetLiveInstances(void* klass);

private:
    enum class LiveFindMode { Type1, Type2, ByType3 };

    bool TryResolveFindApiFromImage(void* image);
    bool EnsureLiveInstanceApiCached();

    UnityResolver& m_resolver;

    std::mutex     m_liveApiCacheMutex{};
    void*          m_cachedFindMethod  = nullptr;
    LiveFindMode   m_cachedFindMode    = LiveFindMode::Type1;
    std::atomic<bool> m_liveApiCacheReady{false};
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
