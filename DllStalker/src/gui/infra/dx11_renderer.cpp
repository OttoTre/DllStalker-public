#include "pch.h"

#ifdef ENABLE_DUMPER

#include "gui/infra/dx11_renderer.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"

namespace Gui::Infra::Dx11
{
bool CreateDevice(HWND hWnd, D3D11Context& context) {
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, context.swapChain.ReleaseAndGetAddressOf(), context.device.ReleaseAndGetAddressOf(), &featureLevel, context.deviceContext.ReleaseAndGetAddressOf()) != S_OK)
        return false;

    CreateRenderTarget(context);
    return true;
}

void CreateRenderTarget(D3D11Context& context) {
    if (!context.swapChain || !context.device) {
        context.mainRenderTargetView.Reset();
        return;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    const HRESULT hr = context.swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.ReleaseAndGetAddressOf()));
    if (FAILED(hr) || !backBuffer) {
        context.mainRenderTargetView.Reset();
        return;
    }

    const HRESULT rtvHr = context.device->CreateRenderTargetView(backBuffer.Get(), nullptr, context.mainRenderTargetView.ReleaseAndGetAddressOf());
    if (FAILED(rtvHr)) {
        context.mainRenderTargetView.Reset();
    }
}

void CleanupRenderTarget(D3D11Context& context) {
    context.mainRenderTargetView.Reset();
}

void CleanupDevice(D3D11Context& context) {
    CleanupRenderTarget(context);
    context.swapChain.Reset();
    context.deviceContext.Reset();
    context.device.Reset();
}

bool IsReady(HWND hwnd, const D3D11Context& context) {
    return context.swapChain && context.deviceContext && !IsIconic(hwnd) && IsWindowVisible(hwnd);
}

void Resize(D3D11Context& context, UINT width, UINT height) {
    if (!context.device || !context.swapChain) {
        return;
    }

    CleanupRenderTarget(context);

    const HRESULT hr = context.swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        // ResizeBuffers failure leaves the swap chain in an undefined state; bail
        // out before recreating the RTV so we don't paper over a deeper problem.
        printf("[!] IDXGISwapChain::ResizeBuffers failed: HRESULT=0x%08lX (w=%u, h=%u)\n",
               static_cast<unsigned long>(hr), width, height);
        return;
    }

    CreateRenderTarget(context);
}

void RenderFrame(D3D11Context& context) {
    ImGui::Render();

    context.deviceContext->OMSetRenderTargets(1, context.mainRenderTargetView.GetAddressOf(), NULL);
    float clear_color[4] = { 0.45f, 0.55f, 0.60f, 1.00f };
    context.deviceContext->ClearRenderTargetView(context.mainRenderTargetView.Get(), clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    context.swapChain->Present(0, 0);
}
} // namespace Gui::Infra::Dx11

#endif // ENABLE_DUMPER
