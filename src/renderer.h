// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <d3d11_1.h>
#include <dcomp.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ImageData;

struct HDRDisplayInfo
{
    bool isHDRCapable = false;
    float maxLuminance = 80.0f;
    float maxFullFrameLuminance = 80.0f;
    float minLuminance = 0.0f;
};

struct ViewportCB
{
    float transform[16]; // 64 bytes — col-major 4x4
    float exposure;      // EV stops
    float gamma;         // typically 1/2.2
    float zoom;          // screen pixels per image pixel
    int isHDR;           // 0=SDR gamma output, 1=HDR scRGB linear
    float sdrWhiteNits;  // 80.0
    float displayMaxNits;
    int showGrid;        // 0=off, 1=on
    int displayMode;     // 0=normal, 1=R, 2=G, 3=B, 4=A
}; // 96 bytes

class Renderer
{
  public:
    bool Initialize(HWND hwnd, bool forceWARP = false);
    void Resize(int width, int height);
    void BeginFrame(float clearR, float clearG, float clearB);
    void EndFrame(bool vsync = true);

    bool UploadImage(const ImageData& image);
    void RenderImage(const ViewportCB& vp);
    bool HasImage() const { return m_imageSRV != nullptr; }

    ID3D11Device1* GetDevice() const { return m_device.Get(); }
    bool IsHDREnabled() const { return m_hdrEnabled; }
    bool SetHDRMode(bool enable);
    const HDRDisplayInfo& GetHDRInfo() const { return m_hdrInfo; }

    bool RefreshHDRInfo(HWND hwnd);

  private:
    bool CreateShaders();
    void CreateRenderTarget();
    void ReleaseRenderTarget();
    HDRDisplayInfo DetectHDR(HWND hwnd);

    ComPtr<IDXGIAdapter> m_adapter;
    ComPtr<ID3D11Device1> m_device;
    ComPtr<ID3D11DeviceContext1> m_context;
    ComPtr<IDXGISwapChain1> m_swapchain;
    ComPtr<ID3D11RenderTargetView> m_rtv;

    // Pipeline state
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11SamplerState> m_linearSampler;
    ComPtr<ID3D11SamplerState> m_pointSampler;

    // Image texture
    ComPtr<ID3D11Texture2D> m_imageTexture;
    ComPtr<ID3D11ShaderResourceView> m_imageSRV;

    int m_width = 0;
    int m_height = 0;

    // DirectComposition (used instead of HWND-based swap chain to avoid
    // DWM/driver stutter when presenting during scroll input)
    ComPtr<IDCompositionDevice> m_dcompDevice;
    ComPtr<IDCompositionTarget> m_dcompTarget;
    ComPtr<IDCompositionVisual> m_dcompVisual;

    // HDR state
    HDRDisplayInfo m_hdrInfo;
    bool m_hdrEnabled = false;
    bool m_useWARP = false;
};
