// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "renderer.h"

#include "image.h"

#include <algorithm>
#include <d3dcompiler.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// Embedded shader source — will be replaced with pre-compiled bytecode later
static const char kShaderSource[] = R"hlsl(
cbuffer ViewportCB : register(b0) {
    float4x4 transform;
    float    exposure;
    float    gamma;
    float    zoom;
    int      isHDR;
    float    sdrWhiteNits;
    float    displayMaxNits;
    int      showGrid;
    int      displayMode;
};

struct VS_INPUT {
    float2 pos : POSITION;
    float2 uv  : TEXCOORD0;
};

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

Texture2D<float4> imageTexture : register(t0);
SamplerState      imageSampler : register(s0);

VS_OUTPUT VSMain(VS_INPUT input) {
    VS_OUTPUT output;
    output.pos = mul(transform, float4(input.pos, 0.0, 1.0));
    output.uv = input.uv;
    return output;
}

float4 PSMain(VS_OUTPUT input) : SV_TARGET {
    float4 hdr = imageTexture.Sample(imageSampler, input.uv);

    // Solo channel modes: extract single channel as grayscale
    // displayMode: 0=normal(+alpha), 1=R, 2=G, 3=B, 4=A, 5=RGB(ignore alpha)
    float3 exposed;
    if (displayMode >= 1 && displayMode <= 4) {
        float ch = (displayMode == 1) ? hdr.r :
                   (displayMode == 2) ? hdr.g :
                   (displayMode == 3) ? hdr.b : hdr.a;
        exposed = ch * exp2(exposure);
    } else {
        exposed = hdr.rgb * exp2(exposure);
    }

    float3 result;
    if (isHDR) {
        result = exposed;
    } else {
        result = pow(saturate(exposed), gamma);
    }

    // Checkerboard transparency — blend image over checker pattern using alpha
    // Only in normal RGB+alpha mode (displayMode 0), not solo channels or ignore-alpha
    if (displayMode == 0) {
        uint texW, texH;
        imageTexture.GetDimensions(texW, texH);
        float2 imgPos = input.uv * float2(texW, texH); // position in image pixels
        float checkerSize = max(8.0, 8.0 / zoom); // 8 screen pixels, but at least 1 image pixel
        int2 cell = int2(floor(imgPos / checkerSize));
        float checker = ((cell.x + cell.y) & 1) ? 0.3 : 0.2;
        if (isHDR)
            checker *= sdrWhiteNits / 80.0;
        result = lerp(checker, result, hdr.a);
    }

    // Pixel grid at high zoom — draw lines at image pixel boundaries
    if (showGrid && zoom >= 8.0) {
        // Get image-space pixel coords from UV (UV 0..1 maps to image dims)
        float2 imgCoord = input.uv * float2(ddx(input.uv).x, ddy(input.uv).y);
        // Use derivatives to find image pixel size in screen pixels
        float2 duvdx = ddx(input.uv);
        float pixelSizeScreen = 1.0 / length(duvdx); // ~zoom level

        // Distance from nearest pixel boundary in UV space
        uint texW, texH;
        imageTexture.GetDimensions(texW, texH);
        float2 imgPos = input.uv * float2(texW, texH);
        float2 fractPos = frac(imgPos);
        float2 distToBorder = min(fractPos, 1.0 - fractPos); // [0, 0.5]

        // Convert to screen pixels
        float2 distScreen = distToBorder * zoom;
        float minDist = min(distScreen.x, distScreen.y);

        // Draw grid line when within ~0.5 screen pixels of a border
        if (minDist < 0.5) {
            float fadeIn = saturate((zoom - 8.0) / 24.0);
            float gridAlpha = fadeIn * 0.5;  // grid color overlay caps at 50%

            // Cap luminance, preserving hue/saturation
            // Ease-in curve so cap kicks in quickly at low zoom
            float capBlend = 1.0 - (1.0 - fadeIn) * (1.0 - fadeIn);
            float lumCap = isHDR ? 0.5 * displayMaxNits / sdrWhiteNits : 0.5;
            float lum = dot(result, float3(0.2126, 0.7152, 0.0722));
            float scale = (lum > lumCap) ? lumCap / lum : 1.0;
            float3 base = lerp(result, result * scale, capBlend);

            // Grid color: offset from base rather than absolute — stays
            // proportional in both dark and bright regions
            float gridContrast = 0.15; // tunable: grid brightness offset
            float baseLum = dot(base, float3(0.2126, 0.7152, 0.0722));
            float3 gridColor = (baseLum > 0.5 * lumCap)
                ? base * (1.0 - gridContrast * 2.0)
                : base + gridContrast;
            result = lerp(base, gridColor, gridAlpha);
        }
    }

    return float4(result, 1.0);
}
)hlsl";

struct Vertex
{
    float x, y;
    float u, v;
};

bool Renderer::Initialize(HWND hwnd, bool forceWARP)
{
    // Create D3D11 device
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    ComPtr<ID3D11Device> baseDevice;
    ComPtr<ID3D11DeviceContext> baseContext;
    HRESULT hr = E_FAIL;

    if (!forceWARP)
    {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, featureLevels,
                               _countof(featureLevels), D3D11_SDK_VERSION, &baseDevice, nullptr, &baseContext);
    }

    if (FAILED(hr))
    {
        // Fall back to WARP (software rasterizer) — works on machines with no GPU,
        // including CI runners.
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, featureLevels, _countof(featureLevels),
                               D3D11_SDK_VERSION, &baseDevice, nullptr, &baseContext);
        if (FAILED(hr))
            return false;
        m_useWARP = true;
        OutputDebugStringW(L"[EXRay] Using WARP software rasterizer\n");
    }

    hr = baseDevice.As(&m_device);
    if (FAILED(hr))
        return false;
    hr = baseContext.As(&m_context);
    if (FAILED(hr))
        return false;

    // Get DXGI factory
    ComPtr<IDXGIDevice1> dxgiDevice;
    m_device.As(&dxgiDevice);

    dxgiDevice->GetAdapter(&m_adapter);

    ComPtr<IDXGIFactory2> factory;
    m_adapter->GetParent(IID_PPV_ARGS(&factory));

    // Detect HDR display capabilities
    m_hdrInfo = DetectHDR(hwnd);

    // Create swapchain
    RECT rc;
    GetClientRect(hwnd, &rc);
    m_width = rc.right - rc.left;
    m_height = rc.bottom - rc.top;

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = m_width;
    desc.Height = m_height;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH;

    if (m_useWARP)
    {
        desc.Flags = 0;
    }
    else
    {
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    }

    if (m_hdrInfo.isHDRCapable)
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    else
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

    // Use DirectComposition swap chain to avoid DWM/driver stutter with
    // HWND-based child window swap chains during rapid Present() calls.
    // Falls back to CreateSwapChainForHwnd if DComp setup fails.
    bool useDComp = !m_useWARP;
    if (useDComp)
    {
        hr = factory->CreateSwapChainForComposition(m_device.Get(), &desc, nullptr, &m_swapchain);
        if (SUCCEEDED(hr))
            hr = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&m_dcompDevice));
        if (SUCCEEDED(hr))
            hr = m_dcompDevice->CreateTargetForHwnd(hwnd, TRUE, &m_dcompTarget);
        if (SUCCEEDED(hr))
            hr = m_dcompDevice->CreateVisual(&m_dcompVisual);
        if (SUCCEEDED(hr))
        {
            m_dcompVisual->SetContent(m_swapchain.Get());
#define USE_OPACITY_HACK
#ifdef USE_OPACITY_HACK
            // Attach an effect group with sub-opaque value to prevent the DWM
            // from promoting this visual to independent flip mode.  The mode
            // *transition* (composed ↔ independent flip) during rapid Present()
            // calls causes system-wide stutter; the effect group forces the DWM
            // to always composite, avoiding the transition.
            // 254/255 ≈ 0.996 opacity — imperceptible (<0.4% luminance), but
            // the DWM cannot optimize it away (identity 3D transforms ARE
            // optimized away and don't prevent promotion).
            ComPtr<IDCompositionEffectGroup> effectGroup;
            if (SUCCEEDED(m_dcompDevice->CreateEffectGroup(&effectGroup)))
            {
                effectGroup->SetOpacity(254.999f / 255.0f);
                m_dcompVisual->SetEffect(effectGroup.Get());
            }
#endif
            m_dcompTarget->SetRoot(m_dcompVisual.Get());
            hr = m_dcompDevice->Commit();
        }

        if (FAILED(hr))
        {
            // DComp failed — clean up and fall back to HWND-based
            m_dcompVisual.Reset();
            m_dcompTarget.Reset();
            m_dcompDevice.Reset();
            m_swapchain.Reset();
            useDComp = false;
        }
    }

    if (!useDComp)
    {
        hr = factory->CreateSwapChainForHwnd(m_device.Get(), hwnd, &desc, nullptr, nullptr, &m_swapchain);
        if (FAILED(hr))
            return false;
    }

    // Set color space for HDR
    if (m_hdrInfo.isHDRCapable)
    {
        ComPtr<IDXGISwapChain3> swapchain3;
        if (SUCCEEDED(m_swapchain.As(&swapchain3)))
        {
            UINT support = 0;
            if (SUCCEEDED(swapchain3->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &support)) &&
                (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
            {
                swapchain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
                m_hdrEnabled = true;
            }
        }
    }

    // Disable Alt+Enter fullscreen
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    CreateRenderTarget();

    if (!CreateShaders())
        return false;

    // Create constant buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(ViewportCB);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    m_device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);

    // Create samplers — linear for zoom-out, point for zoom-in (>100%)
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    sampDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    m_device->CreateSamplerState(&sampDesc, &m_linearSampler);

    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    m_device->CreateSamplerState(&sampDesc, &m_pointSampler);

    // Create index buffer (two triangles)
    UINT16 indices[] = {0, 1, 2, 2, 3, 0};
    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = sizeof(indices);
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibData = {};
    ibData.pSysMem = indices;
    m_device->CreateBuffer(&ibDesc, &ibData, &m_indexBuffer);

    return true;
}

bool Renderer::CreateShaders()
{
    ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

    HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource), "tonemap.hlsl", nullptr, nullptr, "VSMain", "vs_5_0",
                            0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr))
    {
        OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
        return false;
    }

    hr = D3DCompile(kShaderSource, sizeof(kShaderSource), "tonemap.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0,
                    &psBlob, &errorBlob);
    if (FAILED(hr))
    {
        OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
        return false;
    }

    m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vertexShader);
    m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_pixelShader);

    // Input layout
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    m_device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout);

    return true;
}

void Renderer::CreateRenderTarget()
{
    ComPtr<ID3D11Texture2D> backBuffer;
    m_swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtv);
}

void Renderer::ReleaseRenderTarget()
{
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_rtv.Reset();
}

void Renderer::Resize(int width, int height)
{
    if (width <= 0 || height <= 0 || !m_swapchain)
        return;

    m_width = width;
    m_height = height;

    ReleaseRenderTarget();
    UINT swapFlags = m_useWARP ? 0 : DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    m_swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, swapFlags);
    CreateRenderTarget();
}

void Renderer::BeginFrame(float clearR, float clearG, float clearB)
{
    float color[] = {clearR, clearG, clearB, 1.0f};
    m_context->ClearRenderTargetView(m_rtv.Get(), color);
    m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(m_width);
    vp.Height = static_cast<float>(m_height);
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
}

void Renderer::EndFrame(bool vsync) { m_swapchain->Present(vsync ? 1 : 0, 0); }

bool Renderer::UploadImage(const ImageData& image)
{
    m_imageTexture.Reset();
    m_imageSRV.Reset();

    if (!image.IsLoaded())
    {
        m_vertexBuffer.Reset();
        return true;
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = image.width;
    texDesc.Height = image.height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = image.pixels.data();
    initData.SysMemPitch = image.width * 4 * sizeof(float);

    HRESULT hr = m_device->CreateTexture2D(&texDesc, &initData, &m_imageTexture);
    if (FAILED(hr))
        return false;

    hr = m_device->CreateShaderResourceView(m_imageTexture.Get(), nullptr, &m_imageSRV);
    if (FAILED(hr))
        return false;

    // Create/update vertex buffer for this image's dimensions
    Vertex vertices[] = {
        {0.0f, 0.0f, 0.0f, 0.0f},
        {static_cast<float>(image.width), 0.0f, 1.0f, 0.0f},
        {static_cast<float>(image.width), static_cast<float>(image.height), 1.0f, 1.0f},
        {0.0f, static_cast<float>(image.height), 0.0f, 1.0f},
    };

    m_vertexBuffer.Reset();
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = vertices;
    m_device->CreateBuffer(&vbDesc, &vbData, &m_vertexBuffer);

    return true;
}

HDRDisplayInfo Renderer::DetectHDR(HWND hwnd)
{
    HDRDisplayInfo info;
    if (!m_adapter)
        return info;

    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    ComPtr<IDXGIOutput> output;
    for (UINT i = 0; m_adapter->EnumOutputs(i, output.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_OUTPUT_DESC desc;
        if (FAILED(output->GetDesc(&desc)) || desc.Monitor != hmon)
            continue;

        ComPtr<IDXGIOutput6> output6;
        if (SUCCEEDED(output.As(&output6)))
        {
            DXGI_OUTPUT_DESC1 desc1;
            if (SUCCEEDED(output6->GetDesc1(&desc1)))
            {
                info.maxLuminance = desc1.MaxLuminance;
                info.maxFullFrameLuminance = desc1.MaxFullFrameLuminance;
                info.minLuminance = desc1.MinLuminance;
                info.isHDRCapable = (desc1.MaxLuminance > 100.0f);
            }
        }
        break;
    }
    return info;
}

bool Renderer::RefreshHDRInfo(HWND hwnd)
{
    HDRDisplayInfo newInfo = DetectHDR(hwnd);
    bool changed = (newInfo.isHDRCapable != m_hdrInfo.isHDRCapable || newInfo.maxLuminance != m_hdrInfo.maxLuminance);
    m_hdrInfo = newInfo;
    return changed;
}

bool Renderer::SetHDRMode(bool enable)
{
    if (!m_hdrInfo.isHDRCapable || !m_swapchain)
        return false;
    if (enable == m_hdrEnabled)
        return true;

    ReleaseRenderTarget();

    DXGI_FORMAT fmt = enable ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
    UINT swapFlags = m_useWARP ? 0 : DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    HRESULT hr = m_swapchain->ResizeBuffers(2, m_width, m_height, fmt, swapFlags);
    if (FAILED(hr))
    {
        CreateRenderTarget();
        return false;
    }

    ComPtr<IDXGISwapChain3> swapchain3;
    if (SUCCEEDED(m_swapchain.As(&swapchain3)))
    {
        DXGI_COLOR_SPACE_TYPE cs =
            enable ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        swapchain3->SetColorSpace1(cs);
    }

    CreateRenderTarget();
    m_hdrEnabled = enable;
    return true;
}

void Renderer::RenderImage(const ViewportCB& vp)
{
    if (!m_imageSRV)
        return;

    // Update constant buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &vp, sizeof(vp));
    m_context->Unmap(m_constantBuffer.Get(), 0);

    // Set pipeline state
    m_context->IASetInputLayout(m_inputLayout.Get());
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->PSSetShaderResources(0, 1, m_imageSRV.GetAddressOf());

    // Switch to nearest-neighbor sampling when zoomed past 1:1
    auto& sampler = (vp.zoom >= 1.0f) ? m_pointSampler : m_linearSampler;
    m_context->PSSetSamplers(0, 1, sampler.GetAddressOf());

    m_context->DrawIndexed(6, 0, 0);
}
