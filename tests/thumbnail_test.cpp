// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for the thumbnail provider.
// Build & run: bazelisk test //:thumbnail_test

#ifndef UNICODE
#define UNICODE
#endif

#include <initguid.h>
#include "thumbnail_provider.h"

#include <ImfArray.h>
#include <ImfRgbaFile.h>
#include <shlwapi.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

// Provide the global that thumbnail_provider.cpp expects.
long g_dllRefCount = 0;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                                                 \
    static void test_##name();                                                                     \
    struct Register_##name                                                                         \
    {                                                                                              \
        Register_##name() { test_##name(); }                                                       \
    } reg_##name;                                                                                  \
    static void test_##name()

#define EXPECT(expr)                                                                               \
    do                                                                                             \
    {                                                                                              \
        tests_run++;                                                                               \
        if (expr)                                                                                  \
        {                                                                                          \
            tests_passed++;                                                                        \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr);                     \
        }                                                                                          \
    } while (0)

// Write a small solid-color EXR to a temp file and return its path.
static std::wstring CreateTestEXR(int width, int height, float r, float g, float b, float a = 1.0f)
{
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "exray_test";
    fs::create_directories(dir);

    char name[64];
    snprintf(name, sizeof(name), "test_%dx%d.exr", width, height);
    fs::path path = dir / name;

    Imf::Array2D<Imf::Rgba> pixels(height, width);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            pixels[y][x] = Imf::Rgba(half(r), half(g), half(b), half(a));

    Imf::RgbaOutputFile file(path.string().c_str(), width, height, Imf::WRITE_RGBA);
    file.setFrameBuffer(&pixels[0][0], 1, width);
    file.writePixels(height);

    return path.wstring();
}

static void DeleteTestEXR(const std::wstring& path)
{
    std::filesystem::remove(path);
}

// Create an IStream from a file path (what Explorer does for our handler).
static IStream* StreamFromFile(const std::wstring& path)
{
    IStream* pStream = nullptr;
    SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &pStream);
    return pStream;
}

// Create a provider via COM-style heap allocation, initialized with a file stream.
static IThumbnailProvider* MakeProvider(const std::wstring& path)
{
    auto* p = new EXRayThumbnailProvider();
    IStream* pStream = StreamFromFile(path);
    if (pStream)
    {
        p->Initialize(pStream, STGM_READ);
        pStream->Release();
    }
    return p; // caller uses Release() to destroy
}

// Helper to read a pixel from an HBITMAP (top-down 32-bit DIB).
struct BGRA
{
    BYTE b, g, r, a;
};

static BGRA ReadPixel(HBITMAP hbmp, UINT x, UINT y)
{
    BITMAP bm;
    GetObject(hbmp, sizeof(bm), &bm);

    std::vector<BGRA> bits(bm.bmWidth * std::abs(bm.bmHeight));
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = bm.bmWidth;
    bmi.bmiHeader.biHeight = -std::abs(bm.bmHeight); // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = CreateCompatibleDC(nullptr);
    GetDIBits(hdc, hbmp, 0, std::abs(bm.bmHeight), bits.data(), &bmi, DIB_RGB_COLORS);
    DeleteDC(hdc);

    return bits[y * bm.bmWidth + x];
}

// --- Tests ---

TEST(GetThumbnail_basic)
{
    std::wstring path = CreateTestEXR(100, 100, 1.0f, 0.0f, 0.0f);
    IThumbnailProvider* prov = MakeProvider(path);

    HBITMAP hbmp = nullptr;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    HRESULT hr = prov->GetThumbnail(64, &hbmp, &alpha);

    EXPECT(hr == S_OK);
    EXPECT(hbmp != nullptr);
    EXPECT(alpha == WTSAT_ARGB);

    if (hbmp)
    {
        BITMAP bm;
        GetObject(hbmp, sizeof(bm), &bm);
        EXPECT(bm.bmWidth == 64);
        EXPECT(bm.bmHeight == 64);
        DeleteObject(hbmp);
    }

    prov->Release();
    DeleteTestEXR(path);
}

TEST(GetThumbnail_landscape_aspect_ratio)
{
    std::wstring path = CreateTestEXR(200, 100, 0.0f, 1.0f, 0.0f);
    IThumbnailProvider* prov = MakeProvider(path);

    HBITMAP hbmp = nullptr;
    WTS_ALPHATYPE alpha;
    HRESULT hr = prov->GetThumbnail(128, &hbmp, &alpha);

    EXPECT(hr == S_OK);
    if (hbmp)
    {
        BITMAP bm;
        GetObject(hbmp, sizeof(bm), &bm);
        EXPECT(bm.bmWidth == 128); // full width
        EXPECT(bm.bmHeight == 64); // half height (2:1 aspect)
        DeleteObject(hbmp);
    }

    prov->Release();
    DeleteTestEXR(path);
}

TEST(GetThumbnail_portrait_aspect_ratio)
{
    std::wstring path = CreateTestEXR(100, 200, 0.0f, 0.0f, 1.0f);
    IThumbnailProvider* prov = MakeProvider(path);

    HBITMAP hbmp = nullptr;
    WTS_ALPHATYPE alpha;
    HRESULT hr = prov->GetThumbnail(128, &hbmp, &alpha);

    EXPECT(hr == S_OK);
    if (hbmp)
    {
        BITMAP bm;
        GetObject(hbmp, sizeof(bm), &bm);
        EXPECT(bm.bmWidth == 64);  // half width (1:2 aspect)
        EXPECT(bm.bmHeight == 128); // full height
        DeleteObject(hbmp);
    }

    prov->Release();
    DeleteTestEXR(path);
}

TEST(GetThumbnail_tone_mapping)
{
    // A pixel with value 1.0 should tone-map to 0.5, then gamma to ~0.73
    std::wstring path = CreateTestEXR(4, 4, 1.0f, 1.0f, 1.0f);
    IThumbnailProvider* prov = MakeProvider(path);

    HBITMAP hbmp = nullptr;
    WTS_ALPHATYPE alpha;
    prov->GetThumbnail(4, &hbmp, &alpha);

    if (hbmp)
    {
        BGRA px = ReadPixel(hbmp, 0, 0);
        // Reinhard(1.0) = 0.5, gamma(0.5) = 0.5^(1/2.2) ~ 0.73, * 255 ~ 186
        EXPECT(px.r > 170 && px.r < 200);
        EXPECT(px.g > 170 && px.g < 200);
        EXPECT(px.b > 170 && px.b < 200);
        EXPECT(px.a == 255);
        DeleteObject(hbmp);
    }

    prov->Release();
    DeleteTestEXR(path);
}

TEST(GetThumbnail_bright_values_compressed)
{
    // Very bright values (100.0) should be compressed close to 1.0 by Reinhard
    std::wstring path = CreateTestEXR(4, 4, 100.0f, 100.0f, 100.0f);
    IThumbnailProvider* prov = MakeProvider(path);

    HBITMAP hbmp = nullptr;
    WTS_ALPHATYPE alpha;
    prov->GetThumbnail(4, &hbmp, &alpha);

    if (hbmp)
    {
        BGRA px = ReadPixel(hbmp, 0, 0);
        // Reinhard(100) = 100/101 ~ 0.99, gamma ~ 0.995, * 255 ~ 254
        EXPECT(px.r >= 250);
        EXPECT(px.g >= 250);
        EXPECT(px.b >= 250);
        DeleteObject(hbmp);
    }

    prov->Release();
    DeleteTestEXR(path);
}

TEST(GetThumbnail_dark_values)
{
    std::wstring path = CreateTestEXR(4, 4, 0.01f, 0.01f, 0.01f);
    IThumbnailProvider* prov = MakeProvider(path);

    HBITMAP hbmp = nullptr;
    WTS_ALPHATYPE alpha;
    prov->GetThumbnail(4, &hbmp, &alpha);

    if (hbmp)
    {
        BGRA px = ReadPixel(hbmp, 0, 0);
        // Reinhard(0.01) ~ 0.0099, gamma ~ 0.125, * 255 ~ 32
        EXPECT(px.r > 20 && px.r < 50);
        DeleteObject(hbmp);
    }

    prov->Release();
    DeleteTestEXR(path);
}

TEST(GetThumbnail_no_stream)
{
    auto* prov = new EXRayThumbnailProvider();
    // Don't call Initialize — no stream set

    HBITMAP hbmp = nullptr;
    WTS_ALPHATYPE alpha;
    HRESULT hr = prov->GetThumbnail(64, &hbmp, &alpha);

    EXPECT(FAILED(hr));
    EXPECT(hbmp == nullptr);

    prov->Release();
}

TEST(QueryInterface_supported)
{
    auto* prov = new EXRayThumbnailProvider();

    IThumbnailProvider* pThumb = nullptr;
    EXPECT(prov->QueryInterface(IID_IThumbnailProvider, (void**)&pThumb) == S_OK);
    if (pThumb)
        pThumb->Release();

    IInitializeWithStream* pInit = nullptr;
    EXPECT(prov->QueryInterface(IID_IInitializeWithStream, (void**)&pInit) == S_OK);
    if (pInit)
        pInit->Release();

    IUnknown* pUnk = nullptr;
    EXPECT(prov->QueryInterface(IID_IUnknown, (void**)&pUnk) == S_OK);
    if (pUnk)
        pUnk->Release();

    prov->Release();
}

TEST(QueryInterface_unsupported)
{
    auto* prov = new EXRayThumbnailProvider();

    IClassFactory* pFactory = nullptr;
    EXPECT(prov->QueryInterface(IID_IClassFactory, (void**)&pFactory) == E_NOINTERFACE);
    EXPECT(pFactory == nullptr);

    prov->Release();
}

TEST(RefCounting)
{
    auto* prov = new EXRayThumbnailProvider();
    // Starts at 1
    EXPECT(prov->AddRef() == 2);
    EXPECT(prov->AddRef() == 3);
    EXPECT(prov->Release() == 2);
    EXPECT(prov->Release() == 1);
    prov->Release(); // ref hits 0, deletes
}

TEST(ClassFactory_creates_provider)
{
    EXRayClassFactory factory;

    IThumbnailProvider* pThumb = nullptr;
    HRESULT hr = factory.CreateInstance(nullptr, IID_IThumbnailProvider, (void**)&pThumb);
    EXPECT(hr == S_OK);
    EXPECT(pThumb != nullptr);
    if (pThumb)
        pThumb->Release();
}

TEST(ClassFactory_rejects_aggregation)
{
    EXRayClassFactory factory;
    IUnknown* dummy = reinterpret_cast<IUnknown*>(1);
    IThumbnailProvider* pThumb = nullptr;
    HRESULT hr = factory.CreateInstance(dummy, IID_IThumbnailProvider, (void**)&pThumb);
    EXPECT(hr == CLASS_E_NOAGGREGATION);
}

int main()
{
    printf("thumbnail_test: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
