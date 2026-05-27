#include "pch.h"

#include "services/hook_installer.h"
#include "unity_resolver.h"

#ifdef ENABLE_DUMPER
#include "gui/control_panel.h"
#include "services/main_thread_dispatcher.h"
#endif

// Forwarding exports to the REAL system version.dll
#pragma comment(linker, "/export:GetFileVersionInfoA=C:\\Windows\\System32\\version.GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoByHandle=C:\\Windows\\System32\\version.GetFileVersionInfoByHandle")
#pragma comment(linker, "/export:GetFileVersionInfoExA=C:\\Windows\\System32\\version.GetFileVersionInfoExA")
#pragma comment(linker, "/export:GetFileVersionInfoExW=C:\\Windows\\System32\\version.GetFileVersionInfoExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeExW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeExW")
#pragma comment(linker, "/export:GetFileVersionInfoSizeW=C:\\Windows\\System32\\version.GetFileVersionInfoSizeW")
#pragma comment(linker, "/export:GetFileVersionInfoW=C:\\Windows\\System32\\version.GetFileVersionInfoW")
#pragma comment(linker, "/export:VerFindFileA=C:\\Windows\\System32\\version.VerFindFileA")
#pragma comment(linker, "/export:VerFindFileW=C:\\Windows\\System32\\version.VerFindFileW")
#pragma comment(linker, "/export:VerInstallFileA=C:\\Windows\\System32\\version.VerInstallFileA")
#pragma comment(linker, "/export:VerInstallFileW=C:\\Windows\\System32\\version.VerInstallFileW")
#pragma comment(linker, "/export:VerLanguageNameA=C:\\Windows\\System32\\version.VerLanguageNameA")
#pragma comment(linker, "/export:VerLanguageNameW=C:\\Windows\\System32\\version.VerLanguageNameW")
#pragma comment(linker, "/export:VerQueryValueA=C:\\Windows\\System32\\version.VerQueryValueA")
#pragma comment(linker, "/export:VerQueryValueW=C:\\Windows\\System32\\version.VerQueryValueW")

// Post-DllMain bootstrap worker (NOT the process or Unity main thread).
static DWORD WINAPI DllStalkerBootstrap(LPVOID) {
#ifdef ENABLE_DUMPER
    // Tag this thread up front so the dispatcher's runtime_invoke detour
    // doesn't mis-latch us as the engine main thread. Init() itself does
    // not call into runtime_invoke, but defensive ordering: we want the
    // tag set before any code path that could.
    Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
#endif

    if (!Engine::Unity.Init())
    {
        return -1;
    }

    AllocConsole();
    FILE* f; freopen_s(&f, "CONOUT$", "w", stdout);

#ifdef ENABLE_DUMPER
    // Install the runtime_invoke hook so the Method Invoker can dispatch
    // managed calls onto the engine's main thread (the first non-DllStalker
    // thread to reach the detour latches as the main thread). Failure is
    // non-fatal; the GUI surfaces a "Method Invoker disabled" status and
    // keeps Run buttons disabled rather than falling back to an unsafe
    // direct call from the GUI thread.
    Engine::Services::MainThreadDispatcher::InstallRuntimeInvokeHook();
#endif

    // Preset selection is a compile-time string baked into hook_installer;
    // the registry resolves the target assembly image and dispatches.
    Hooks::StartHooking();

    printf("[*] Unity engine initialized successfully.\n");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)DllStalkerBootstrap, NULL, 0, NULL);

        #ifdef ENABLE_DUMPER
            // GUI should be created on a dedicated thread.
            CreateThread(NULL, 0, Gui::CreateControlPanelThread, NULL, 0, NULL);
        #endif
    }
    return TRUE;
}
