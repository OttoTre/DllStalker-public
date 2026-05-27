#pragma once

#include "pch.h"

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

namespace Gui::Infra::Dx11
{
struct D3D11Context {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> mainRenderTargetView;
};

bool CreateDevice(HWND hWnd, D3D11Context& context);
void CreateRenderTarget(D3D11Context& context);
void CleanupRenderTarget(D3D11Context& context);
void CleanupDevice(D3D11Context& context);
bool IsReady(HWND hwnd, const D3D11Context& context);
void Resize(D3D11Context& context, UINT width, UINT height);
void RenderFrame(D3D11Context& context);
} // namespace Gui::Infra::Dx11
