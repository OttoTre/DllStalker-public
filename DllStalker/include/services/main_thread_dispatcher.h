#pragma once

#include "build_config.h"

#ifdef ENABLE_DUMPER

#include <functional>
#include <windows.h>

namespace Engine::Services
{
// Cross-thread bridge that runs jobs on the Unity main thread.
//
// Why this exists: the dumper's GUI runs on its own dedicated thread (see
// [src/dllmain.cpp] DllStalkerBootstrap). Calling `il2cpp_runtime_invoke` /
// `mono_runtime_invoke` from there crashes on any method that touches the
// scene graph, transforms, or UI -- which covers a large portion of game
// code.
//
// Solution: hook the runtime's `runtime_invoke` export itself. The engine
// drives that function every frame from its main thread (every MonoBehaviour
// callback, coroutine tick, or UI event ultimately routes through it). On
// each call, our detour drains a small batch of jobs that the GUI / worker
// threads have enqueued, then forwards to the original. We identify "the
// main thread" by latching the first thread id that reaches the detour from
// outside our own DllStalker threads.
//
// Lifecycle: call InstallRuntimeInvokeHook() exactly once at startup, AFTER
// Engine::Unity.Init() has resolved the runtime_invoke export. Each thread
// that we spawn ourselves should call TagCurrentThreadAsOurs() at its entry
// so it is excluded from main-thread latching (otherwise the GUI thread,
// which also calls runtime_invoke for FindObjectsOfType, would mis-latch as
// the main thread and we'd drain on the wrong thread).
namespace MainThreadDispatcher
{
using Job = std::function<void()>;

// ---- Thread tagging ----
// Mark the calling thread as a DllStalker-owned thread. The runtime_invoke
// detour skips main-thread latching and drain dispatch when the caller is
// tagged. Idempotent; cheap (one TLS write).
void TagCurrentThreadAsOurs();

// ---- Queue + install ----
// Push a job onto the queue. Safe to call from any thread. Returns false
// if job is empty. When full, drops the oldest job and enqueues the new one.
bool Enqueue(Job job);

// Push a job without drop-oldest semantics. Returns false if job is empty
// or the queue is already at capacity. Used by synchronized script commands.
bool TryEnqueueNoDrop(Job job);

// Install MinHook on Engine::Unity.invoker.Raw(). Returns true on success.
// Idempotent: subsequent calls return the cached install state. Must be
// called after Engine::Unity.Init() so the export address is resolved.
bool InstallRuntimeInvokeHook();

// ---- Status accessors (UI-friendly) ----
// True when the runtime_invoke hook is installed (the queue can drain).
bool IsDispatchAvailable();

// True once the detour has observed at least one non-DllStalker call and
// latched a thread id. Until then we don't know which thread is the main
// thread, so the UI keeps Run disabled.
bool IsMainThreadCaptured();

// 0 until latched; otherwise the captured thread id.
DWORD GetMainThreadId();

// Monotonic count of jobs dropped because the queue hit kMaxQueueDepth.
uint32_t GetDroppedJobCount();

// Snapshot of pending jobs (under internal mutex).
uint32_t GetQueueDepth();

// True when the captured Unity main thread is known and matches the caller.
bool IsOnMainThread();

// GetTickCount64() timestamp of the most recent dispatcher drain batch; 0 if never drained.
uint64_t GetLastDrainTickMs();

// Panel teardown: reject further Enqueue/TryEnqueueNoDrop and drop queued jobs.
// Does not drain via runtime_invoke (safe from the GUI thread). Idempotent.
void BeginShutdown();
} // namespace MainThreadDispatcher
} // namespace Engine::Services

#endif // ENABLE_DUMPER
