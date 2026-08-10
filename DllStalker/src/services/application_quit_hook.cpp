#include "pch.h"

#ifdef ENABLE_DUMPER

#include "services/application_quit_hook.h"

#include "services/bootstrap_log.h"
#include "services/hook_installer.h"
#include "unity_resolver.h"

#include <atomic>
#include <mutex>
#include <string>

namespace Engine::Services::ApplicationQuitHook
{
namespace
{
// IL2CPP x64: instance/static methods pass MethodInfo* after managed args.
// Quit()      -> RCX = MethodInfo*
// Quit(int)   -> RCX = exitCode, RDX = MethodInfo*
// Separate trampoline types so notify() cannot smash a live second arg.
using Quit0Fn = void(__fastcall*)(void* methodInfo);
using Quit1Fn = void(__fastcall*)(int exitCode, void* methodInfo);

Quit0Fn                      g_originalQuit0 = nullptr;
Quit1Fn                      g_originalQuit1 = nullptr;
std::atomic<QuitNotifyFn>    g_quitNotify{nullptr};
std::atomic<bool>            g_installed{false};
std::once_flag               g_installOnce;

void NotifyQuit() {
    if (const QuitNotifyFn notify = g_quitNotify.load(std::memory_order_acquire)) {
        notify();
    }
}

void __fastcall ApplicationQuit0Detour(void* methodInfo) {
    NotifyQuit();
    if (g_originalQuit0) {
        g_originalQuit0(methodInfo);
    }
}

void __fastcall ApplicationQuit1Detour(int exitCode, void* methodInfo) {
    NotifyQuit();
    if (g_originalQuit1) {
        g_originalQuit1(exitCode, methodInfo);
    }
}

void* ResolveQuitImage() {
    static const char* kImageCandidates[] = {
        "UnityEngine.CoreModule",
        "UnityEngine",
    };

    constexpr int kOnce = 1;
    for (const char* imageName : kImageCandidates) {
        void* image = Engine::Unity.FindImageExact(imageName, kOnce);
        if (!image) {
            const std::string withDll = std::string(imageName) + ".dll";
            image = Engine::Unity.FindImageExact(withDll.c_str(), kOnce);
        }
        if (!image) {
            image = Engine::Unity.FindImage(imageName, kOnce);
        }
        if (image) {
            return image;
        }
    }
    return nullptr;
}

bool InstallOnce() {
    void* image = ResolveQuitImage();
    if (!image) {
        Engine::Services::BootstrapLog::Write(
            "[!] ApplicationQuitHook: UnityEngine image not found; "
            "Application.Quit host-quit disabled.\n");
        return false;
    }

    // Prefer parameterless Quit; fall back to Quit(int exitCode) only.
    const uintptr_t quit0 =
        Engine::Unity.GetMethodAddress(image, "Application", "Quit", 0, "UnityEngine");
    if (quit0) {
        if (!Hooks::InstallHook(reinterpret_cast<LPVOID>(quit0),
                                reinterpret_cast<LPVOID>(&ApplicationQuit0Detour),
                                reinterpret_cast<LPVOID*>(&g_originalQuit0),
                                "Application.Quit")) {
            Engine::Services::BootstrapLog::Write(
                "[!] ApplicationQuitHook: InstallHook failed for Application.Quit at %p\n",
                reinterpret_cast<void*>(quit0));
            return false;
        }
        Engine::Services::BootstrapLog::Write(
            "[+] ApplicationQuitHook: hooked Application.Quit (0-arg) at %p\n",
            reinterpret_cast<void*>(quit0));
        return true;
    }

    const uintptr_t quit1 =
        Engine::Unity.GetMethodAddress(image, "Application", "Quit", 1, "UnityEngine");
    if (!quit1) {
        Engine::Services::BootstrapLog::Write(
            "[!] ApplicationQuitHook: Application.Quit not resolved; "
            "host-quit via Quit disabled.\n");
        return false;
    }

    if (!Hooks::InstallHook(reinterpret_cast<LPVOID>(quit1),
                            reinterpret_cast<LPVOID>(&ApplicationQuit1Detour),
                            reinterpret_cast<LPVOID*>(&g_originalQuit1),
                            "Application.Quit")) {
        Engine::Services::BootstrapLog::Write(
            "[!] ApplicationQuitHook: InstallHook failed for Application.Quit at %p\n",
            reinterpret_cast<void*>(quit1));
        return false;
    }

    Engine::Services::BootstrapLog::Write(
        "[+] ApplicationQuitHook: hooked Application.Quit (1-arg) at %p\n",
        reinterpret_cast<void*>(quit1));
    return true;
}
} // namespace

void SetQuitNotify(QuitNotifyFn fn) {
    g_quitNotify.store(fn, std::memory_order_release);
}

bool Install() {
    std::call_once(g_installOnce, [] {
        g_installed.store(InstallOnce(), std::memory_order_release);
    });
    return g_installed.load(std::memory_order_acquire);
}

} // namespace Engine::Services::ApplicationQuitHook

#endif // ENABLE_DUMPER
