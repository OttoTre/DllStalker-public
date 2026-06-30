#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/control_panel.h"

#include "gui/app/app_shell.h"
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

// Applies Config::GUI_SCALE to font size and widget metrics.
void ApplyGuiScale()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    io.Fonts->AddFontDefault();
    io.FontGlobalScale = Config::GUI_SCALE;

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(Config::GUI_SCALE);
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
        bool hadInputMessage = false;
        running = Infra::ProcessControlPanelMessages(msg, hadInputMessage);
        if (!running) {
            break;
        }

        if (!Infra::Dx11::IsReady(window.hwnd, g_d3d)) {
            Sleep(16);
            continue;
        }

        // Poll ~10 Hz when timer-driven UI is active; otherwise wait on input (INFINITE).
        const bool wantsPeriodicTick =
            state.fieldsAutoRefresh || state.fieldWatch.HasActiveEntriesCount() != 0
            || state.transformModel.liveRefresh
            || state.scriptModel.IsRuntimeActive();
        const DWORD idleWakeMs = wantsPeriodicTick ? 100u : INFINITE;
        if (Infra::WaitForRenderTriggerIfNeeded(requestRender, hadInputMessage, idleWakeMs)) {
            continue;
        }

        if (Infra::WaitForFramePacingIfNeeded(nextFrameAt)) {
            continue;
        }

        state.scriptModel.Pump();

        AppShell::BeginControlPanelFrame(window.hwnd);
        AppShell::RenderControlPanelContent(state);
        Infra::Dx11::RenderFrame(g_d3d);
        Infra::UpdateFramePacing(hadInputMessage, requestRender, nextFrameAt);
    }
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
