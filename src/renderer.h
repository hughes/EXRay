// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <d3d11_1.h>
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
    float _pad;
}; // 96 bytes

struct HistogramCB
{
    float panelLeft, panelTop, panelWidth, panelHeight; // NDC coords
    int channelMode;                                    // 0=Lum, 1=R, 2=G, 3=B, 4=All
    float log2Min, log2Max;
    int binCount;                                       // number of histogram bins (e.g. 512)
    float tfExposure, tfGamma;
    int tfIsHDR;
    float sdrWhiteNits, displayMaxNits;
    float _pad1[3];
}; // 64 bytes

struct HistogramData;

class Renderer
{
  public:
    bool Initialize(HWND hwnd);
    void Resize(int width, int height);
    void BeginFrame(float clearR, float clearG, float clearB);
    void EndFrame(bool vsync = true);

    bool UploadImage(const ImageData& image);
    void RenderImage(const ViewportCB& vp);
    bool HasImage() const { return m_imageSRV != nullptr; }

    void UploadHistogram(const HistogramData& histogram);
    void RenderHistogram(const HistogramCB& cb);

    ID3D11Device1* GetDevice() const { return m_device.Get(); }
    bool IsHDREnabled() const { return m_hdrEnabled; }
    bool SetHDRMode(bool enable);
    const HDRDisplayInfo& GetHDRInfo() const { return m_hdrInfo; }

    bool RefreshHDRInfo(HWND hwnd);

  private:
    bool CreateShaders();
    bool CreateHistogramShaders();
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

    // Histogram overlay
    ComPtr<ID3D11VertexShader> m_histogramVS;
    ComPtr<ID3D11PixelShader> m_histogramPS;
    ComPtr<ID3D11Buffer> m_histogramCB;
    ComPtr<ID3D11Texture2D> m_histogramTexture;
    ComPtr<ID3D11ShaderResourceView> m_histogramSRV;
    // Background snapshot for histogram compositing
    ComPtr<ID3D11Texture2D> m_histBgTexture;
    ComPtr<ID3D11ShaderResourceView> m_histBgSRV;
    DXGI_FORMAT m_histBgFormat = DXGI_FORMAT_UNKNOWN;

    // HDR state
    HDRDisplayInfo m_hdrInfo;
    bool m_hdrEnabled = false;
};
