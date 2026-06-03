#pragma once

#ifdef ENABLE_DUMPER

#include <mutex>
#include <vector>

#include "unity_resolver.h"

namespace Engine::Dumper
{
// Wraps UnityEngine.Object::FindObjectsOfType{,ByType} so callers can
// retrieve every live MonoBehaviour-derived instance of a class without
// caring about which signature variant the target Unity version exposes.
//
// Owns:
//   * The dumper-only export resolution (InitDumperExports). The plan
//     parks this here because it's the only place that runs introspection
//     against the engine before the GUI ever opens.
//   * A set-once cache of the FindObjectsOfType method handle, populated
//     via std::call_once so the hot path is lock-free.
class LiveObjectFinder
{
public:
    explicit LiveObjectFinder(UnityResolver& resolver);

    // Run once at construction
    bool InitDumperExports();

    // Returns the System.Type wrapper for `klass` so it can be passed to
    // FindObjectsOfType. Null on failure.
    void* GetSystemTypeForClass(void* klass);

    // Live snapshot of every loaded instance of `klass`. Empty on any
    // engine-side error (the caller treats that as "not found").
    std::vector<void*> GetLiveInstances(void* klass);

private:
    enum class LiveFindMode { Type1, Type2, ByType3 };

    UnityResolver& m_resolver;

    std::once_flag m_liveApiCacheOnce;
    void*          m_cachedFindMethod  = nullptr;
    LiveFindMode   m_cachedFindMode    = LiveFindMode::Type1;
    bool           m_liveApiCacheReady = false;

    bool EnsureLiveInstanceApiCached();
};
} // namespace Engine::Dumper

#endif // ENABLE_DUMPER
