#ifndef UNICODE
#define UNICODE
#endif

#include "renderer.h"

#include "histogram.h"
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
    float    _pad;
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
    float3 exposed = hdr.rgb * exp2(exposure);

    float3 result;
    if (isHDR) {
        result = exposed;
    } else {
        result = pow(saturate(exposed), gamma);
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

// Histogram overlay shader — full-screen triangle, procedural bars + exposure bracket
static const char kHistogramShaderSource[] = R"hlsl(
cbuffer HistogramCB : register(b1) {
    float panelLeft, panelTop, panelWidth, panelHeight;
    int   channelMode;
    float log2Min, log2Max;
    int   binCount;
    float tfExposure, tfGamma;
    int   tfIsHDR;
    float sdrWhiteNits, displayMaxNits;
    float3 _pad1;
};

Texture2D<float> histogramTex : register(t1); // binCount x 4: rows = Lum,R,G,B
Texture2D        bgTexture    : register(t2); // snapshot of backbuffer under panel

struct VS_OUT {
    float4 pos : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

VS_OUT HistVS(uint vid : SV_VertexID) {
    VS_OUT o;
    o.ndc.x = (vid == 1) ?  3.0 : -1.0;
    o.ndc.y = (vid == 2) ? -3.0 :  1.0;
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}

float4 HistPS(VS_OUT input) : SV_TARGET {
    float2 ndc = input.ndc;

    float localX = (ndc.x - panelLeft) / panelWidth;
    float localY = (panelTop - ndc.y) / panelHeight;

    if (localX < 0.0 || localX > 1.0 || localY < 0.0 || localY > 1.0)
        discard;

    // Sample the image underneath, clamped so HDR can't blow out the overlay
    static const int kPanelW = 300;
    static const int kPanelH = 100;
    float3 bgImage = bgTexture.Load(int3(
        int(localX * (kPanelW - 1)),
        int(localY * (kPanelH - 1)), 0)).rgb;
    bgImage = saturate(bgImage); // clamp to [0,1] — no-op in SDR, caps HDR

    float barY = 1.0 - localY;
    int bin = clamp(int(localX * (binCount - 1)), 0, binCount - 1);
    float range = log2Max - log2Min;

    // --- Exposure zones in log2 scene space ---
    float rangeInv = (range > 0.0) ? 1.0 / range : 0.0;

    // Dark crush: ~1% output brightness
    float log2Black = (1.0 / tfGamma) * log2(0.01) - tfExposure;
    float blackX = (log2Black - log2Min) * rangeInv;

    // SDR white: scene value that maps to 1.0 output
    float sdrWhiteX = (-tfExposure - log2Min) * rangeInv;

    // HDR clip: display max luminance
    float hdrWhiteX = sdrWhiteX;
    if (tfIsHDR && sdrWhiteNits > 0.0)
        hdrWhiteX = (log2(displayMaxNits / sdrWhiteNits) - tfExposure - log2Min) * rangeInv;

    // Background brightness per zone — crushed zone stays black,
    // normal range is lifted so the dark zone stands out by contrast
    float zoneBg = 0.08; // SDR visible range — slightly lifted
    float barScale = 1.0;
    if (localX < blackX) {
        zoneBg = 0.0;     // crushed shadows — pure black
        barScale = 0.6;
    } else if (tfIsHDR && localX > hdrWhiteX) {
        zoneBg = 0.18;    // HDR clipped
        barScale = 1.15;
    } else if (tfIsHDR && localX > sdrWhiteX) {
        zoneBg = 0.12;    // HDR extension (above SDR white)
        barScale = 1.08;
    } else if (!tfIsHDR && localX > sdrWhiteX) {
        zoneBg = 0.18;    // SDR clipped
        barScale = 1.15;
    }

    // Histogram overlay color + blend weight (alpha used as mix factor below)
    float4 color = float4(zoneBg, zoneBg, zoneBg, 0.65);

    // --- Draw histogram bars ---
    if (channelMode == 4) {
        float rVal = histogramTex.Load(int3(bin, 1, 0));
        float gVal = histogramTex.Load(int3(bin, 2, 0));
        float bVal = histogramTex.Load(int3(bin, 3, 0));

        if (barY < rVal) color = float4(0.7, 0.15, 0.15, 0.75);
        if (barY < gVal) color.g = max(color.g, 0.7);
        if (barY < bVal) color.b = max(color.b, 0.7);
        if (barY < rVal || barY < gVal || barY < bVal) {
            color.rgb *= barScale;
            color.a = 0.75;
        }
    } else {
        int row = channelMode;
        float val = histogramTex.Load(int3(bin, row, 0));

        float3 barColor = float3(0.7, 0.7, 0.7);
        if (channelMode == 1) barColor = float3(0.85, 0.2, 0.2);
        if (channelMode == 2) barColor = float3(0.2, 0.85, 0.2);
        if (channelMode == 3) barColor = float3(0.3, 0.3, 0.85);

        if (barY < val) {
            color = float4(barColor * barScale, 0.75);
        }
    }

    // Composite in-shader: replaces hardware alpha blend with clamped background
    float3 result = color.rgb * color.a + bgImage * (1.0 - color.a);
    return float4(result, 1.0);
}
)hlsl";

struct Vertex
{
    float x, y;
    float u, v;
};

bool Renderer::Initialize(HWND hwnd)
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
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, featureLevels,
                                   _countof(featureLevels), D3D11_SDK_VERSION, &baseDevice, nullptr, &baseContext);

    if (FAILED(hr))
        return false;

    hr = baseDevice.As(&m_device);
    if (FAILED(hr))
        return false;
    hr = baseContext.As(&m_context);
    if (FAILED(hr))
        return false;

    // Get DXGI factory
    ComPtr<IDXGIDevice1> dxgiDevice;
    m_device.As(&dxgiDevice);

    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);

    ComPtr<IDXGIFactory2> factory;
    adapter->GetParent(IID_PPV_ARGS(&factory));

    // Detect HDR display capabilities
    m_hdrInfo = DetectHDR(adapter.Get());

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
    desc.Scaling = DXGI_SCALING_NONE;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    if (m_hdrInfo.isHDRCapable)
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    else
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

    hr = factory->CreateSwapChainForHwnd(m_device.Get(), hwnd, &desc, nullptr, nullptr, &m_swapchain);
    if (FAILED(hr))
        return false;

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

    if (!CreateHistogramShaders())
        return false;

    // Create constant buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(ViewportCB);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    m_device->CreateBuffer(&cbDesc, nullptr, &m_constantBuffer);

    // Create histogram constant buffer
    {
        D3D11_BUFFER_DESC hcbDesc = {};
        hcbDesc.ByteWidth = sizeof(HistogramCB);
        hcbDesc.Usage = D3D11_USAGE_DYNAMIC;
        hcbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hcbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        m_device->CreateBuffer(&hcbDesc, nullptr, &m_histogramCB);
    }

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
    // Force staging texture recreation (backbuffer format may change)
    m_histBgTexture.Reset();
    m_histBgSRV.Reset();
    m_histBgFormat = DXGI_FORMAT_UNKNOWN;
}

void Renderer::Resize(int width, int height)
{
    if (width <= 0 || height <= 0 || !m_swapchain)
        return;

    m_width = width;
    m_height = height;

    ReleaseRenderTarget();
    m_swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN,
                               DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
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

HDRDisplayInfo Renderer::DetectHDR(IDXGIAdapter* adapter)
{
    HDRDisplayInfo info;
    ComPtr<IDXGIOutput> output;
    if (SUCCEEDED(adapter->EnumOutputs(0, &output)))
    {
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
    }
    return info;
}

bool Renderer::SetHDRMode(bool enable)
{
    if (!m_hdrInfo.isHDRCapable || !m_swapchain)
        return false;
    if (enable == m_hdrEnabled)
        return true;

    ReleaseRenderTarget();

    DXGI_FORMAT fmt = enable ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
    HRESULT hr =
        m_swapchain->ResizeBuffers(2, m_width, m_height, fmt, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
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

bool Renderer::CreateHistogramShaders()
{
    ComPtr<ID3DBlob> vsBlob, psBlob, errorBlob;

    HRESULT hr = D3DCompile(kHistogramShaderSource, sizeof(kHistogramShaderSource), "histogram.hlsl", nullptr, nullptr,
                            "HistVS", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr))
    {
        OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
        return false;
    }

    hr = D3DCompile(kHistogramShaderSource, sizeof(kHistogramShaderSource), "histogram.hlsl", nullptr, nullptr,
                    "HistPS", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr))
    {
        OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
        return false;
    }

    m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_histogramVS);
    m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_histogramPS);

    return true;
}

void Renderer::UploadHistogram(const HistogramData& histo)
{
    m_histogramTexture.Reset();
    m_histogramSRV.Reset();

    // 256x4 R32_FLOAT texture: row 0=Lum, 1=R, 2=G, 3=B
    float data[4 * HistogramData::kBinCount];
    memcpy(data + 0 * HistogramData::kBinCount, histo.luminance.data(), HistogramData::kBinCount * sizeof(float));
    memcpy(data + 1 * HistogramData::kBinCount, histo.red.data(), HistogramData::kBinCount * sizeof(float));
    memcpy(data + 2 * HistogramData::kBinCount, histo.green.data(), HistogramData::kBinCount * sizeof(float));
    memcpy(data + 3 * HistogramData::kBinCount, histo.blue.data(), HistogramData::kBinCount * sizeof(float));

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = HistogramData::kBinCount;
    texDesc.Height = 4;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = data;
    initData.SysMemPitch = HistogramData::kBinCount * sizeof(float);

    m_device->CreateTexture2D(&texDesc, &initData, &m_histogramTexture);
    m_device->CreateShaderResourceView(m_histogramTexture.Get(), nullptr, &m_histogramSRV);
}

void Renderer::RenderHistogram(const HistogramCB& cb)
{
    if (!m_histogramSRV)
        return;

    // --- Snapshot the backbuffer region under the histogram panel ---
    constexpr int kPanelW = 300;
    constexpr int kPanelH = 100;

    // NDC → pixel coordinates
    int srcX = static_cast<int>((cb.panelLeft + 1.0f) * 0.5f * m_width);
    int srcY = static_cast<int>((1.0f - cb.panelTop) * 0.5f * m_height);
    int copyW = (std::min)(kPanelW, m_width - srcX);
    int copyH = (std::min)(kPanelH, m_height - srcY);

    if (copyW > 0 && copyH > 0)
    {
        // Get the backbuffer texture
        ComPtr<ID3D11Texture2D> backBuffer;
        m_swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));

        D3D11_TEXTURE2D_DESC bbDesc;
        backBuffer->GetDesc(&bbDesc);

        // (Re)create staging texture if format changed or first use
        if (m_histBgFormat != bbDesc.Format)
        {
            m_histBgTexture.Reset();
            m_histBgSRV.Reset();

            D3D11_TEXTURE2D_DESC texDesc = {};
            texDesc.Width = kPanelW;
            texDesc.Height = kPanelH;
            texDesc.MipLevels = 1;
            texDesc.ArraySize = 1;
            texDesc.Format = bbDesc.Format;
            texDesc.SampleDesc.Count = 1;
            texDesc.Usage = D3D11_USAGE_DEFAULT;
            texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            m_device->CreateTexture2D(&texDesc, nullptr, &m_histBgTexture);
            m_device->CreateShaderResourceView(m_histBgTexture.Get(), nullptr, &m_histBgSRV);
            m_histBgFormat = bbDesc.Format;
        }

        // Copy the panel region from the backbuffer
        D3D11_BOX srcBox = {};
        srcBox.left = srcX;
        srcBox.top = srcY;
        srcBox.right = srcX + copyW;
        srcBox.bottom = srcY + copyH;
        srcBox.front = 0;
        srcBox.back = 1;
        m_context->CopySubresourceRegion(m_histBgTexture.Get(), 0, 0, 0, 0, backBuffer.Get(), 0, &srcBox);
    }

    // Update histogram constant buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    m_context->Map(m_histogramCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &cb, sizeof(cb));
    m_context->Unmap(m_histogramCB.Get(), 0);

    // No vertex buffer — full-screen triangle via SV_VertexID
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_context->VSSetShader(m_histogramVS.Get(), nullptr, 0);
    m_context->PSSetShader(m_histogramPS.Get(), nullptr, 0);
    m_context->PSSetConstantBuffers(1, 1, m_histogramCB.GetAddressOf());

    ID3D11ShaderResourceView* srvs[2] = {m_histogramSRV.Get(), m_histBgSRV.Get()};
    m_context->PSSetShaderResources(1, 2, srvs);

    m_context->Draw(3, 0);

    // Unbind SRVs
    ID3D11ShaderResourceView* nullSRVs[2] = {nullptr, nullptr};
    m_context->PSSetShaderResources(1, 2, nullSRVs);
}
