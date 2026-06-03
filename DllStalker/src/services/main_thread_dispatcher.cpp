#include "pch.h"

#ifdef ENABLE_DUMPER

#include "services/main_thread_dispatcher.h"

#include "MinHook.h"
#include "services/hook_installer.h"
#include "unity_resolver.h"

#include <atomic>
#include <deque>
#include <mutex>

namespace Engine::Services::MainThreadDispatcher
{
namespace
{
// Bounded drain so a runaway producer can't stall the main thread / frame.
// Each runtime_invoke call drains at most this many queued jobs before
// forwarding to the original.
constexpr size_t kMaxJobsPerDrain = 16;

// Queue cap so Run-spam clicks can't grow the deque without bound. Hitting
// the cap drops the oldest job and prints a console warning.
constexpr size_t kMaxQueueDepth = 64;

UnityExports::t_RuntimeInvoke g_originalRuntimeInvoke = nullptr;
std::atomic<bool>       g_hookInstalled         = false;
std::once_flag          g_installOnce;

// We store the first non-DllStalker thread id that calls runtime_invoke.
// compare_exchange makes this a one-time write, even if many threads race.
// This captured id is treated as the Unity main thread.
std::atomic<DWORD>      g_mainThreadId          = 0;

// Marker for our own threads (GUI, DllStalkerBootstrap worker, jthread
// workers). The detour skips both the latch and the drain when this is set,
// so our own runtime_invoke calls (e.g. FindObjectsOfType from the Live API
// search) never get mistaken for the engine main thread and never re-trigger
// drain work from a worker thread.
thread_local bool       tl_isOurThread          = false;

// Prevent recursive draining on the same thread.
// A drained job may call runtime_invoke again, which re-enters this detour.
// This thread-local flag avoids nested drain loops.
thread_local bool       tl_drainingOnThisThread = false;

std::mutex              g_queueMutex;
std::deque<Job>         g_queue;
std::atomic<uint32_t>   g_droppedJobCount{0};

// This wrapper uses SEH to survive access violations from queued jobs.
// Regular C++ catch blocks do not catch AV under /EHsc.
// Keep SEH isolated in this helper.
__declspec(noinline) void RunJobWithSEH(Job& job) noexcept {
    __try {
        job();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Survive. The session-state worker that wrapped this lambda
        // already populated a result struct; even if it didn't get to
        // populate the success/failure fields, the user is no worse off
        // than they were before this hook existed.
    }
}

// Drains up to kMaxJobsPerDrain jobs. Holds the mutex only while popping;
// each job runs without the lock so a long-running invoke can't block
// Enqueue from other threads.
void DrainOnce() {
    for (size_t i = 0; i < kMaxJobsPerDrain; ++i) {
        Job job;
        {
            std::lock_guard<std::mutex> lock(g_queueMutex);
            if (g_queue.empty()) return;
            job = std::move(g_queue.front());
            g_queue.pop_front();
        }
        RunJobWithSEH(job);
    }
}

// Fast decision path for runtime_invoke detour.
// Most calls should return quickly without draining.
// Keeping this logic small reduces overhead on hot calls.
void MaybeDrain() {
    if (tl_isOurThread)       return;   // skip our own threads entirely
    if (tl_drainingOnThisThread) return; // reentrancy guard

    const DWORD tid = GetCurrentThreadId();

    // Heuristic: the first non-DllStalker caller of runtime_invoke is the Unity main thread.
    // This is usually true because managed startup runs on the main thread first.
    DWORD expected = 0;
    if (g_mainThreadId.compare_exchange_strong(expected, tid) && expected == 0) {
        printf("[+] MainThreadDispatcher: captured main thread id 0x%lX\n",
               static_cast<unsigned long>(tid));
    }

    if (g_mainThreadId.load() != tid) return;

    tl_drainingOnThisThread = true;
    DrainOnce();
    tl_drainingOnThisThread = false;
}

void* __cdecl RuntimeInvokeDetour(void* method, void* obj, void** params, void** exc) {
    MaybeDrain();
    return g_originalRuntimeInvoke(method, obj, params, exc);
}

bool InstallRuntimeInvokeHookOnce() {
    void* target = reinterpret_cast<void*>(Engine::Unity.invoker.Raw());
    if (!target) {
        printf("[!] MainThreadDispatcher: fnRuntimeInvoke not resolved; "
               "Method Invoker disabled.\n");
        return false;
    }

    Hooks::EnsureMinHookInitialized();

    if (!Hooks::TryRegisterHookTarget(reinterpret_cast<uintptr_t>(target))) {
        printf("[!] MainThreadDispatcher: runtime_invoke target already hooked\n");
        return false;
    }

    if (MH_CreateHook(target, reinterpret_cast<LPVOID>(&RuntimeInvokeDetour),
                       reinterpret_cast<LPVOID*>(&g_originalRuntimeInvoke)) != MH_OK) {
        printf("[!] MainThreadDispatcher: MH_CreateHook(runtime_invoke @ %p) failed\n", target);
        Hooks::UnregisterHookTarget(reinterpret_cast<uintptr_t>(target));
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        printf("[!] MainThreadDispatcher: MH_EnableHook(runtime_invoke @ %p) failed\n", target);
        MH_RemoveHook(target);
        Hooks::UnregisterHookTarget(reinterpret_cast<uintptr_t>(target));
        return false;
    }

    printf("[+] MainThreadDispatcher: hooked runtime_invoke @ %p\n", target);
    return true;
}
} // namespace

void TagCurrentThreadAsOurs() {
    tl_isOurThread = true;
}

bool Enqueue(Job job) {
    if (!job) return false;

    std::lock_guard<std::mutex> lock(g_queueMutex);
    if (g_queue.size() >= kMaxQueueDepth) {
        // Drop the oldest to keep the queue bounded; prevents click-spam
        // from forcing unbounded memory growth or starving newer requests.
        g_droppedJobCount.fetch_add(1, std::memory_order_relaxed);
        printf("[!] MainThreadDispatcher: queue full (%zu); dropping oldest job\n",
               g_queue.size());
        g_queue.pop_front();
    }
    g_queue.push_back(std::move(job));
    return true;
}

bool InstallRuntimeInvokeHook() {
    std::call_once(g_installOnce, [] {
        g_hookInstalled.store(InstallRuntimeInvokeHookOnce());
    });
    return g_hookInstalled.load();
}

bool IsDispatchAvailable() {
    return g_hookInstalled.load();
}

bool IsMainThreadCaptured() {
    return g_mainThreadId.load() != 0;
}

DWORD GetMainThreadId() {
    return g_mainThreadId.load();
}

uint32_t GetDroppedJobCount() {
    return g_droppedJobCount.load(std::memory_order_relaxed);
}

uint32_t GetQueueDepth() {
    std::lock_guard<std::mutex> lock(g_queueMutex);
    return static_cast<uint32_t>(g_queue.size());
}
} // namespace Engine::Services::MainThreadDispatcher

#endif // ENABLE_DUMPER
