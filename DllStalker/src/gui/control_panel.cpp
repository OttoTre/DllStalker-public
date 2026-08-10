#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/control_panel.h"

#include "gui/app/app_shell.h"
#include "gui/chrome/ui_theme.h"
#include "gui/config.h"
#include "gui/infra/dx11_renderer.h"
#include "gui/infra/imgui_context_guard.h"
#include "gui/infra/message_pump.h"
#include "gui/session_state.h"
#include "gui/window.h"
#include "services/main_thread_dispatcher.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

namespace Gui
{
namespace
{
Infra::Dx11::D3D11Context g_d3d{};

void OnWindowResize(UINT width, UINT height) {
    if (g_d3d.device != nullptr) {
        Infra::Dx11::Resize(g_d3d, width, height);
    }
}

void ApplyGuiScale()
{
    UiTheme::LoadFonts();
    UiTheme::ApplyDllStalkerTheme();
}

// --- Initialization helpers ---
bool InitializeWindow(Window::ControlPanelWindow& window) {
    Sleep(100); // Small delay to prioritize the main game thread's window creation
    return Window::Create(window, L"DllStalker", L"DllStalker - Control Panel", OnWindowResize,
                          Config::DEFAULT_WINDOW_WIDTH, Config::DEFAULT_WINDOW_HEIGHT);
}

bool InitializeRenderer(const Window::ControlPanelWindow& window) {
    if (!Infra::Dx11::CreateDevice(window.hwnd, g_d3d)) {
        Infra::Dx11::CleanupDevice(g_d3d);
        return false;
    }
    return true;
}

bool InitializeImGui(const Window::ControlPanelWindow& window, Infra::ImGuiContextRAII& imguiGuard) {
    imguiGuard.Create();
    if (!imguiGuard.IsValid()) {
        return false;
    }

    // Same-frame key/char batches in one NewFrame (ImGui ≥ 1.87).
#if IMGUI_VERSION_NUM >= 18700
    ImGui::GetIO().ConfigInputTrickleEventQueue = false;
#endif

    ApplyGuiScale();

    ImGui_ImplWin32_Init(window.hwnd);
    ImGui_ImplDX11_Init(g_d3d.device.Get(), g_d3d.deviceContext.Get());
    return true;
}

// --- Main loop ---
void RunControlPanelMainLoop(const Window::ControlPanelWindow& window) {
    ControlPanelSessionState state{};

    MSG msg = { 0 };
    bool running = true;
    bool requestRender = true;
    auto nextFrameAt = std::chrono::steady_clock::now();

    while (running) {
        // Host quit detector (UnityWndClass / process top-level IsWindow poll).
        Window::PollHostQuit();

        bool hadInputMessage = false;
        running = Infra::ProcessControlPanelMessages(msg, hadInputMessage);
        if (!running) {
            break;
        }

        // Latch Present before host-quit / Dx / idle / pacing continues — Sleep+continue
        // must not drop the only wake for drained input.
        requestRender = requestRender || hadInputMessage;

        // Host quit: pump until WM_QUIT; skip Present. Manual X still uses the path below.
        if (Window::IsHostQuitting()) {
            if (!hadInputMessage) {
                MsgWaitForMultipleObjectsEx(0, nullptr, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
            continue;
        }

        if (!Infra::Dx11::IsReady(window.hwnd, g_d3d)) {
            Sleep(16);
            continue;
        }

        // Gate sticky Class filter on Classes tab only (sidebarBrowserMode 0);
        // otherwise an unflushed buffer keeps Present waking on Search.
        const bool classFilterPending =
            state.sidebarBrowserMode == 0
            && strcmp(state.classFilterBuffer, state.cachedOriginalFilter.c_str()) != 0;
        const bool wantsPeriodicTick =
            state.fieldsAutoRefresh || state.fieldWatch.HasActiveEntriesCount() != 0
            || state.transformModel.liveRefresh
            || state.scriptModel.IsRuntimeActive()
            || state.loaders.valueSearchInProgress.load(std::memory_order_relaxed)
            || classFilterPending;
        // Idle path cannot use INFINITE: soft Quit destroys the game HWND
        // without posting to our panel, so PollHostQuit needs periodic wakes.
        // Timeout-only wakes must not Present (unlike wantsPeriodicTick).
        constexpr DWORD kHostQuitPollMs = 250;
        if (wantsPeriodicTick) {
            if (Infra::WaitForRenderTriggerIfNeeded(
                    requestRender, hadInputMessage,
                    static_cast<DWORD>(Infra::kIdleFrameBudgetMs))) {
                continue;
            }
        } else if (!requestRender && !hadInputMessage) {
            MsgWaitForMultipleObjectsEx(0, nullptr, kHostQuitPollMs, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            continue;
        }

        // Clamp leftover idle nextFrameAt so the first key after idle is not delayed;
        // continuous input still honors UpdateFramePacing's cap.
        if (hadInputMessage) {
            const auto now = std::chrono::steady_clock::now();
            const auto inputHorizon =
                now + std::chrono::milliseconds(Infra::kInputFrameBudgetMs);
            if (nextFrameAt > inputHorizon) {
                nextFrameAt = now;
            }
        }
        if (Infra::WaitForFramePacingIfNeeded(nextFrameAt)) {
            continue;
        }

        state.scriptModel.Pump();
        AppShell::TickBeforePaint(state);

        AppShell::BeginControlPanelFrame(window.hwnd);
        AppShell::RenderControlPanelContent(state);
        Infra::Dx11::RenderFrame(g_d3d);
        Infra::UpdateFramePacing(hadInputMessage, requestRender, nextFrameAt);
    }

    // Before ControlPanelSessionState destructor joins jthreads.
    state.BeginShutdown();
}

// --- Cleanup helpers ---
void ShutdownImGui() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
}

void ShutdownRenderer() {
    Infra::Dx11::CleanupDevice(g_d3d);
}

void ShutdownWindow(Window::ControlPanelWindow& window) {
    Window::Destroy(window);
}
} // namespace

// --- Public API ---
void CreateControlPanel() {
    if (Infra::IsCrashHandler()) {
        return;
    }

    Window::ControlPanelWindow window{};
    Infra::ImGuiContextRAII imguiGuard;

    if (!InitializeWindow(window)) {
        return;
    }

    if (!InitializeRenderer(window)) {
        ShutdownWindow(window);
        return;
    }

    Window::Show(window);

    if (!InitializeImGui(window, imguiGuard)) {
        ShutdownRenderer();
        ShutdownWindow(window);
        return;
    }

    RunControlPanelMainLoop(window);

    ShutdownImGui();
    ShutdownRenderer();
    ShutdownWindow(window);
}

DWORD WINAPI CreateControlPanelThread(LPVOID) {
    // Tag this thread BEFORE any code path that might end up calling
    // runtime_invoke (Live API search, etc.). The dispatcher's detour
    // uses this flag to skip its main-thread latch on our own threads.
    Engine::Services::MainThreadDispatcher::TagCurrentThreadAsOurs();
    Gui::CreateControlPanel();
    return 0;
}

} // namespace Gui

#endif // ENABLE_DUMPER
