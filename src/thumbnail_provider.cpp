// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnail_provider.h"

#include <ImfArray.h>
#include <ImfChromaticities.h>
#include <ImfIO.h>
#include <ImfRgbaFile.h>
#include <ImfStandardAttributes.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

// Adapter: wraps a COM IStream as an OpenEXR Imf::IStream so RgbaInputFile
// can read directly from the stream Explorer provides.
class ComStreamAdapter : public Imf::IStream
{
  public:
    ComStreamAdapter(::IStream* pStream)
        : Imf::IStream("<explorer stream>"), m_pStream(pStream)
    {
        m_pStream->AddRef();
    }

    ~ComStreamAdapter() override
    {
        m_pStream->Release();
    }

    bool read(char c[], int n) override
    {
        ULONG totalRead = 0;
        while (totalRead < static_cast<ULONG>(n))
        {
            ULONG bytesRead = 0;
            HRESULT hr = m_pStream->Read(c + totalRead, n - totalRead, &bytesRead);
            if (FAILED(hr))
                throw std::runtime_error("IStream read failed");
            if (bytesRead == 0)
                throw std::runtime_error("Unexpected end of stream");
            totalRead += bytesRead;
        }
        return false; // not at EOF after a successful full read
    }

    uint64_t tellg() override
    {
        LARGE_INTEGER zero = {};
        ULARGE_INTEGER pos;
        HRESULT hr = m_pStream->Seek(zero, STREAM_SEEK_CUR, &pos);
        if (FAILED(hr))
            throw std::runtime_error("IStream seek failed");
        return pos.QuadPart;
    }

    void seekg(uint64_t pos) override
    {
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(pos);
        HRESULT hr = m_pStream->Seek(li, STREAM_SEEK_SET, nullptr);
        if (FAILED(hr))
            throw std::runtime_error("IStream seek failed");
    }

    void clear() override {}

  private:
    ::IStream* m_pStream;
};

EXRayThumbnailProvider::EXRayThumbnailProvider() : m_refCount(1), m_pStream(nullptr)
{
    InterlockedIncrement(&g_dllRefCount);
}

EXRayThumbnailProvider::~EXRayThumbnailProvider()
{
    if (m_pStream)
        m_pStream->Release();
    InterlockedDecrement(&g_dllRefCount);
}

IFACEMETHODIMP EXRayThumbnailProvider::QueryInterface(REFIID riid, void** ppv)
{
    if (riid == IID_IUnknown || riid == IID_IThumbnailProvider)
    {
        *ppv = static_cast<IThumbnailProvider*>(this);
    }
    else if (riid == IID_IInitializeWithStream)
    {
        *ppv = static_cast<IInitializeWithStream*>(this);
    }
    else
    {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) EXRayThumbnailProvider::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

IFACEMETHODIMP_(ULONG) EXRayThumbnailProvider::Release()
{
    long count = InterlockedDecrement(&m_refCount);
    if (count == 0)
        delete this;
    return count;
}

IFACEMETHODIMP EXRayThumbnailProvider::Initialize(IStream* pstream, DWORD)
{
    if (m_pStream)
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    m_pStream = pstream;
    m_pStream->AddRef();
    return S_OK;
}

IFACEMETHODIMP EXRayThumbnailProvider::GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha)
{
    *phbmp = nullptr;
    *pdwAlpha = WTSAT_ARGB;

    if (!m_pStream)
        return E_UNEXPECTED;

    try
    {
        ComStreamAdapter adapter(m_pStream);
        Imf::RgbaInputFile file(adapter);

        // Compute color matrix for chromaticity conversion
        static const Imf::Chromaticities kRec709; // default is Rec. 709
        float colorMtx[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        bool hasColorMatrix = false;
        if (Imf::hasChromaticities(file.header()))
        {
            Imf::Chromaticities src = Imf::chromaticities(file.header());
            auto close = [](const Imath::V2f& a, const Imath::V2f& b)
            { return std::fabs(a.x - b.x) < 1e-4f && std::fabs(a.y - b.y) < 1e-4f; };
            if (!(close(src.red, kRec709.red) && close(src.green, kRec709.green) && close(src.blue, kRec709.blue) &&
                  close(src.white, kRec709.white)))
            {
                Imath::M44f srcM = Imf::RGBtoXYZ(src, 1.0f);
                Imath::M44f dstM = Imf::RGBtoXYZ(kRec709, 1.0f);
                Imath::M44f dstInv = dstM.inverse();
                // Imath uses row-vector convention (v*M), but we apply as M*v (column-vector),
                // so we need: combo = (src * dstInv), then transpose during extraction.
                Imath::M44f combo = srcM * dstInv;
                for (int r = 0; r < 3; r++)
                    for (int c = 0; c < 3; c++)
                        colorMtx[r * 3 + c] = combo[c][r]; // transpose
                hasColorMatrix = true;
            }
        }

        Imath::Box2i dw = file.dataWindow();

        int imgW = dw.max.x - dw.min.x + 1;
        int imgH = dw.max.y - dw.min.y + 1;
        if (imgW <= 0 || imgH <= 0)
            return E_FAIL;

        // Compute thumbnail dimensions maintaining aspect ratio
        UINT thumbW, thumbH;
        if (imgW >= imgH)
        {
            thumbW = cx;
            thumbH = static_cast<UINT>(static_cast<float>(cx) * imgH / imgW);
        }
        else
        {
            thumbH = cx;
            thumbW = static_cast<UINT>(static_cast<float>(cx) * imgW / imgH);
        }
        thumbW = (std::max)(thumbW, 1u);
        thumbH = (std::max)(thumbH, 1u);

        // Create a top-down 32-bit DIB section
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = static_cast<LONG>(thumbW);
        bmi.bmiHeader.biHeight = -static_cast<LONG>(thumbH); // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        BYTE* pBits = nullptr;
        HBITMAP hbmp =
            CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pBits), nullptr, 0);
        if (!hbmp)
            return E_OUTOFMEMORY;

        // Read only the scanlines we need. For a 4K image at 256px thumb,
        // this reads ~16x fewer scanlines than loading the full image.
        float scaleX = static_cast<float>(imgW) / thumbW;
        float scaleY = static_cast<float>(imgH) / thumbH;

        // Single-row buffer for reading one scanline at a time
        std::vector<Imf::Rgba> rowBuf(imgW);

        int lastReadY = -1; // track which scanline is in the buffer

        for (UINT y = 0; y < thumbH; ++y)
        {
            int srcY = std::clamp(static_cast<int>((y + 0.5f) * scaleY), 0, imgH - 1);
            int absY = srcY + dw.min.y;

            // Only read this scanline if we haven't already
            if (srcY != lastReadY)
            {
                file.setFrameBuffer(rowBuf.data() - dw.min.x - static_cast<int64_t>(absY) * imgW, 1, imgW);
                file.readPixels(absY, absY);
                lastReadY = srcY;
            }

            for (UINT x = 0; x < thumbW; ++x)
            {
                int srcX = std::clamp(static_cast<int>((x + 0.5f) * scaleX), 0, imgW - 1);
                const Imf::Rgba& px = rowBuf[srcX];

                float rf = static_cast<float>(px.r);
                float gf = static_cast<float>(px.g);
                float bf = static_cast<float>(px.b);
                float af = std::clamp(static_cast<float>(px.a), 0.0f, 1.0f);

                // Apply chromaticity conversion before tone mapping
                if (hasColorMatrix)
                {
                    float r2 = colorMtx[0] * rf + colorMtx[1] * gf + colorMtx[2] * bf;
                    float g2 = colorMtx[3] * rf + colorMtx[4] * gf + colorMtx[5] * bf;
                    float b2 = colorMtx[6] * rf + colorMtx[7] * gf + colorMtx[8] * bf;
                    rf = r2;
                    gf = g2;
                    bf = b2;
                }

                rf = (std::max)(0.0f, rf);
                gf = (std::max)(0.0f, gf);
                bf = (std::max)(0.0f, bf);

                // Reinhard tone mapping
                rf = rf / (1.0f + rf);
                gf = gf / (1.0f + gf);
                bf = bf / (1.0f + bf);

                // sRGB gamma
                rf = std::pow(rf, 1.0f / 2.2f);
                gf = std::pow(gf, 1.0f / 2.2f);
                bf = std::pow(bf, 1.0f / 2.2f);

                // Premultiply alpha for WTSAT_ARGB
                rf *= af;
                gf *= af;
                bf *= af;

                // Write BGRA
                BYTE* dst = pBits + (y * thumbW + x) * 4;
                dst[0] = static_cast<BYTE>(bf * 255.0f + 0.5f);
                dst[1] = static_cast<BYTE>(gf * 255.0f + 0.5f);
                dst[2] = static_cast<BYTE>(rf * 255.0f + 0.5f);
                dst[3] = static_cast<BYTE>(af * 255.0f + 0.5f);
            }
        }

        *phbmp = hbmp;
        return S_OK;
    }
    catch (...)
    {
        return E_FAIL;
    }
}

// ClassFactory

IFACEMETHODIMP EXRayClassFactory::QueryInterface(REFIID riid, void** ppv)
{
    if (riid == IID_IUnknown || riid == IID_IClassFactory)
    {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

IFACEMETHODIMP_(ULONG) EXRayClassFactory::AddRef() { return 2; }
IFACEMETHODIMP_(ULONG) EXRayClassFactory::Release() { return 1; }

IFACEMETHODIMP EXRayClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv)
{
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;

    auto* provider = new (std::nothrow) EXRayThumbnailProvider();
    if (!provider)
        return E_OUTOFMEMORY;

    HRESULT hr = provider->QueryInterface(riid, ppv);
    provider->Release();
    return hr;
}

IFACEMETHODIMP EXRayClassFactory::LockServer(BOOL fLock)
{
    if (fLock)
        InterlockedIncrement(&g_dllRefCount);
    else
        InterlockedDecrement(&g_dllRefCount);
    return S_OK;
}
