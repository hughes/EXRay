// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "image.h"

#include <ImfArray.h>
#include <ImfChannelList.h>
#include <ImfChromaticities.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfInputPart.h>
#include <ImfMultiPartInputFile.h>
#include <ImfRgbaFile.h>
#include <ImfStandardAttributes.h>
#include <ImfTileDescription.h>
#include <ImfTiledInputPart.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <windows.h>

static std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return {};
    int len =
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string result(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(), len, nullptr, nullptr);
    return result;
}

// Reject images that would require more than 2 GB of pixel data.
// This catches corrupt headers before we attempt a massive allocation.
static constexpr size_t kMaxPixelBytes = size_t(2) * 1024 * 1024 * 1024;

// --- Chromaticity → Rec. 709 color matrix computation ---

// Rec. 709 / sRGB primaries and D65 white point
static const Imf::Chromaticities kRec709;

static bool ChromaticitiesMatch(const Imf::Chromaticities& a, const Imf::Chromaticities& b, float eps = 1e-4f)
{
    auto close = [eps](const Imath::V2f& u, const Imath::V2f& v)
    { return std::fabs(u.x - v.x) < eps && std::fabs(u.y - v.y) < eps; };
    return close(a.red, b.red) && close(a.green, b.green) && close(a.blue, b.blue) && close(a.white, b.white);
}

// Invert a 3x3 matrix (row-major). Returns false if singular.
static bool Invert3x3(const float m[9], float inv[9])
{
    float det =
        m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::fabs(det) < 1e-12f)
        return false;
    float id = 1.0f / det;
    inv[0] = (m[4] * m[8] - m[5] * m[7]) * id;
    inv[1] = (m[2] * m[7] - m[1] * m[8]) * id;
    inv[2] = (m[1] * m[5] - m[2] * m[4]) * id;
    inv[3] = (m[5] * m[6] - m[3] * m[8]) * id;
    inv[4] = (m[0] * m[8] - m[2] * m[6]) * id;
    inv[5] = (m[2] * m[3] - m[0] * m[5]) * id;
    inv[6] = (m[3] * m[7] - m[4] * m[6]) * id;
    inv[7] = (m[1] * m[6] - m[0] * m[7]) * id;
    inv[8] = (m[0] * m[4] - m[1] * m[3]) * id;
    return true;
}

// Multiply two 3x3 row-major matrices: out = A * B
static void Mul3x3(const float a[9], const float b[9], float out[9])
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            out[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c] + a[r * 3 + 1] * b[1 * 3 + c] + a[r * 3 + 2] * b[2 * 3 + c];
}

// Extract the upper-left 3x3 from an Imath::M44f and transpose it.
// Imath uses row-vector convention (v * M); we need column-vector (M * v)
// for the HLSL shader, so we transpose during extraction.
static void ExtractM33Transposed(const Imath::M44f& m, float out[9])
{
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            out[r * 3 + c] = m[c][r];
}

// Bradford chromatic adaptation matrix
static constexpr float kBradford[9] = {0.8951f, 0.2664f, -0.1614f, -0.7502f, 1.7135f,
                                       0.0367f, 0.0389f, -0.0685f, 1.0296f};
static constexpr float kBradfordInv[9] = {0.9870f, -0.1471f, 0.1600f, 0.4323f, 0.5184f,
                                          0.0493f, -0.0085f, 0.0400f, 0.9685f};

// Compute Bradford chromatic adaptation from source white XYZ to dest white XYZ
static void BradfordAdaptation(const float srcWhiteXYZ[3], const float dstWhiteXYZ[3], float out[9])
{
    // Cone response for source and dest
    float srcCone[3], dstCone[3];
    for (int i = 0; i < 3; i++)
    {
        srcCone[i] = kBradford[i * 3 + 0] * srcWhiteXYZ[0] + kBradford[i * 3 + 1] * srcWhiteXYZ[1] +
                     kBradford[i * 3 + 2] * srcWhiteXYZ[2];
        dstCone[i] = kBradford[i * 3 + 0] * dstWhiteXYZ[0] + kBradford[i * 3 + 1] * dstWhiteXYZ[1] +
                     kBradford[i * 3 + 2] * dstWhiteXYZ[2];
    }

    // Diagonal scaling in cone space
    float scale[9] = {};
    for (int i = 0; i < 3; i++)
        scale[i * 3 + i] = (std::fabs(srcCone[i]) > 1e-10f) ? dstCone[i] / srcCone[i] : 1.0f;

    // M_adapt = BradfordInv * Scale * Bradford
    float tmp[9];
    Mul3x3(scale, kBradford, tmp);
    Mul3x3(kBradfordInv, tmp, out);
}

// CIE xy → XYZ (Y=1)
static void WhiteXYZ(const Imath::V2f& white, float xyz[3])
{
    xyz[0] = white.x / white.y;
    xyz[1] = 1.0f;
    xyz[2] = (1.0f - white.x - white.y) / white.y;
}

// Identify well-known chromaticity sets by their primaries
static std::string IdentifyColorSpace(const Imf::Chromaticities& c)
{
    // ACEScg (AP1)
    Imf::Chromaticities ap1;
    ap1.red = {0.713f, 0.293f};
    ap1.green = {0.165f, 0.830f};
    ap1.blue = {0.128f, 0.044f};
    ap1.white = {0.32168f, 0.33767f};
    if (ChromaticitiesMatch(c, ap1, 5e-3f))
        return "ACEScg";

    // ACES 2065-1 (AP0)
    Imf::Chromaticities ap0;
    ap0.red = {0.7347f, 0.2653f};
    ap0.green = {0.0f, 1.0f};
    ap0.blue = {0.0001f, -0.077f};
    ap0.white = {0.32168f, 0.33767f};
    if (ChromaticitiesMatch(c, ap0, 5e-3f))
        return "ACES AP0";

    // DCI-P3 (D65)
    Imf::Chromaticities p3;
    p3.red = {0.680f, 0.320f};
    p3.green = {0.265f, 0.690f};
    p3.blue = {0.150f, 0.060f};
    p3.white = {0.3127f, 0.3290f};
    if (ChromaticitiesMatch(c, p3, 5e-3f))
        return "DCI-P3";

    return "Wide Gamut";
}

// Compute a 3x3 color matrix to convert from the file's chromaticities to Rec. 709.
// Stores identity if no conversion is needed. Sets outName to the identified color space.
static void ComputeColorMatrix(const Imf::Header& header, float outMatrix[9], std::string& outName)
{
    // Default to identity
    outMatrix[0] = 1;
    outMatrix[1] = 0;
    outMatrix[2] = 0;
    outMatrix[3] = 0;
    outMatrix[4] = 1;
    outMatrix[5] = 0;
    outMatrix[6] = 0;
    outMatrix[7] = 0;
    outMatrix[8] = 1;
    outName.clear();

    if (!Imf::hasChromaticities(header))
        return;

    Imf::Chromaticities src = Imf::chromaticities(header);

    if (ChromaticitiesMatch(src, kRec709))
        return;

    outName = IdentifyColorSpace(src);

    // Source RGB → XYZ matrix (from OpenEXR)
    Imath::M44f srcToXYZ44 = Imf::RGBtoXYZ(src, 1.0f);
    float srcToXYZ[9];
    ExtractM33Transposed(srcToXYZ44, srcToXYZ);

    // Rec. 709 RGB → XYZ matrix, then invert to get XYZ → Rec. 709
    Imath::M44f rec709ToXYZ44 = Imf::RGBtoXYZ(kRec709, 1.0f);
    float rec709ToXYZ[9];
    ExtractM33Transposed(rec709ToXYZ44, rec709ToXYZ);

    float xyzToRec709[9];
    if (!Invert3x3(rec709ToXYZ, xyzToRec709))
        return; // shouldn't happen, but fall back to identity

    // Check if white points differ — apply Bradford chromatic adaptation
    float srcWhite[3], dstWhite[3];
    WhiteXYZ(src.white, srcWhite);
    WhiteXYZ(kRec709.white, dstWhite);

    bool whitePointsDiffer =
        std::fabs(srcWhite[0] - dstWhite[0]) > 1e-4f || std::fabs(srcWhite[2] - dstWhite[2]) > 1e-4f;

    if (whitePointsDiffer)
    {
        float adapt[9];
        BradfordAdaptation(srcWhite, dstWhite, adapt);

        // Final = XYZtoRec709 * Adapt * SrcToXYZ
        float tmp[9];
        Mul3x3(adapt, srcToXYZ, tmp);
        Mul3x3(xyzToRec709, tmp, outMatrix);
    }
    else
    {
        // Final = XYZtoRec709 * SrcToXYZ
        Mul3x3(xyzToRec709, srcToXYZ, outMatrix);
    }
}

bool ImageLoader::LoadEXR(const std::wstring& filePath, ImageData& outImage, std::string& errorMsg)
{
    try
    {
        std::string narrowPath = WideToUtf8(filePath);

        Imf::RgbaInputFile file(narrowPath.c_str());
        Imath::Box2i dw = file.dataWindow();

        int width = dw.max.x - dw.min.x + 1;
        int height = dw.max.y - dw.min.y + 1;

        if (width <= 0 || height <= 0)
        {
            errorMsg = "Invalid image dimensions.";
            return false;
        }

        size_t pixelBytes = static_cast<size_t>(width) * height * 4 * sizeof(float);
        if (pixelBytes > kMaxPixelBytes)
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "Image too large (%d x %d, %.0f MB). Maximum is 2 GB.", width, height,
                     static_cast<double>(pixelBytes) / (1024.0 * 1024.0));
            errorMsg = buf;
            return false;
        }

        Imf::Array2D<Imf::Rgba> halfPixels(height, width);
        file.setFrameBuffer(&halfPixels[0][0] - dw.min.x - dw.min.y * width, 1, width);
        file.readPixels(dw.min.y, dw.max.y);

        outImage.width = width;
        outImage.height = height;
        outImage.pixels.resize(static_cast<size_t>(width) * height * 4);

        bool allZeroAlpha = true;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const Imf::Rgba& src = halfPixels[y][x];
                float* dst = &outImage.pixels[static_cast<size_t>((y * width + x) * 4)];
                dst[0] = static_cast<float>(src.r);
                dst[1] = static_cast<float>(src.g);
                dst[2] = static_cast<float>(src.b);
                dst[3] = static_cast<float>(src.a);
                allZeroAlpha = allZeroAlpha && (dst[3] == 0.0f);
            }
        }
        outImage.alphaAllZero = allZeroAlpha;

        ComputeColorMatrix(file.header(), outImage.colorMatrix, outImage.colorSpace);

        return true;
    }
    catch (const std::bad_alloc&)
    {
        errorMsg = "Not enough memory to load this image.";
        return false;
    }
    catch (const std::exception& e)
    {
        errorMsg = e.what();
        return false;
    }
    catch (...)
    {
        errorMsg = "Unknown error while reading file.";
        return false;
    }
}

bool ImageLoader::ScanLayers(const std::wstring& filePath, ExrFileInfo& outInfo, std::string& errorMsg)
{
    try
    {
        std::string narrowPath = WideToUtf8(filePath);
        Imf::MultiPartInputFile file(narrowPath.c_str());

        outInfo = {};
        outInfo.partCount = file.parts();

        for (int p = 0; p < outInfo.partCount; p++)
        {
            const Imf::Header& header = file.header(p);
            const Imf::ChannelList& channels = header.channels();

            // Get part name if available
            std::string partName;
            if (header.hasName())
                partName = header.name();

            // Check if this part is tiled and has mipmaps
            bool isTiled = header.hasTileDescription();
            int numMipLevels = 1;
            std::vector<std::pair<int, int>> mipDimensions; // (width, height) per level

            Imath::Box2i dw = header.dataWindow();
            int baseWidth = dw.max.x - dw.min.x + 1;
            int baseHeight = dw.max.y - dw.min.y + 1;

            if (isTiled)
            {
                Imf::TiledInputPart tiledPart(file, p);
                numMipLevels = tiledPart.numLevels();
                for (int lv = 0; lv < numMipLevels; lv++)
                    mipDimensions.push_back({tiledPart.levelWidth(lv), tiledPart.levelHeight(lv)});
            }
            else
            {
                mipDimensions.push_back({baseWidth, baseHeight});
            }

            // Collect all channel names
            std::vector<std::string> allChannels;
            for (auto it = channels.begin(); it != channels.end(); ++it)
                allChannels.push_back(it.name());

            // Group channels by layer prefix
            std::set<std::string> layerNames;
            channels.layers(layerNames);

            // Helper: populate tiled/mip fields and push layer (once per mip level)
            auto addLayerWithMips = [&](ExrLayer baseLayer)
            {
                if (numMipLevels <= 1)
                {
                    // Single level (scanline or tiled ONE_LEVEL)
                    baseLayer.isTiled = isTiled;
                    baseLayer.mipLevel = 0;
                    baseLayer.numMipLevels = 1;
                    baseLayer.mipWidth = mipDimensions[0].first;
                    baseLayer.mipHeight = mipDimensions[0].second;
                    outInfo.layers.push_back(std::move(baseLayer));
                }
                else
                {
                    // Multiple mip levels — create one entry per level
                    for (int lv = 0; lv < numMipLevels; lv++)
                    {
                        ExrLayer mipLayer = baseLayer;
                        mipLayer.isTiled = true;
                        mipLayer.mipLevel = lv;
                        mipLayer.numMipLevels = numMipLevels;
                        mipLayer.mipWidth = mipDimensions[lv].first;
                        mipLayer.mipHeight = mipDimensions[lv].second;
                        outInfo.layers.push_back(std::move(mipLayer));
                    }
                }
            };

            // Root-level channels (no dot prefix)
            {
                ExrLayer rootLayer;
                rootLayer.name = "";
                rootLayer.partIndex = p;
                rootLayer.partName = partName;
                for (const auto& ch : allChannels)
                {
                    if (ch.find('.') == std::string::npos)
                        rootLayer.channels.push_back(ch);
                }
                if (!rootLayer.channels.empty())
                    addLayerWithMips(std::move(rootLayer));
            }

            // Named layers
            for (const auto& layerName : layerNames)
            {
                ExrLayer layer;
                layer.name = layerName;
                layer.partIndex = p;
                layer.partName = partName;

                Imf::ChannelList::ConstIterator first, last;
                channels.channelsInLayer(layerName, first, last);
                for (auto it = first; it != last; ++it)
                {
                    // Store just the suffix after the layer prefix
                    std::string fullName = it.name();
                    std::string suffix = fullName.substr(layerName.size() + 1);
                    layer.channels.push_back(suffix);
                }
                if (!layer.channels.empty())
                    addLayerWithMips(std::move(layer));
            }
        }

        return true;
    }
    catch (const std::exception& e)
    {
        errorMsg = e.what();
        return false;
    }
    catch (...)
    {
        errorMsg = "Unknown error scanning EXR layers.";
        return false;
    }
}

// Try to find a channel matching one of the candidate suffixes
static std::string FindChannel(const std::vector<std::string>& channels, const std::vector<std::string>& candidates)
{
    for (const auto& c : candidates)
    {
        for (const auto& ch : channels)
        {
            if (ch == c)
                return ch;
            // Case-insensitive fallback
            std::string lower = ch;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            std::string lowerC = c;
            std::transform(lowerC.begin(), lowerC.end(), lowerC.begin(), ::tolower);
            if (lower == lowerC)
                return ch;
        }
    }
    return {};
}

ChannelMapping MapChannelsToRGBA(const std::vector<std::string>& channels)
{
    ChannelMapping m;

    // Try RGB names first
    m.rChannel = FindChannel(channels, {"R", "r", "red", "Red"});
    m.gChannel = FindChannel(channels, {"G", "g", "green", "Green"});
    m.bChannel = FindChannel(channels, {"B", "b", "blue", "Blue"});
    m.aChannel = FindChannel(channels, {"A", "a", "alpha", "Alpha"});

    // Fall back to XYZ mapping only if all three are present
    if (m.rChannel.empty() && m.gChannel.empty() && m.bChannel.empty())
    {
        std::string xName = FindChannel(channels, {"X", "x"});
        std::string yName = FindChannel(channels, {"Y", "y"});
        std::string zName = FindChannel(channels, {"Z", "z"});
        if (!xName.empty() && !yName.empty() && !zName.empty())
        {
            m.rChannel = xName;
            m.gChannel = yName;
            m.bChannel = zName;
        }
    }

    // If no RGB or XYZ mapping, display as grayscale
    if (m.rChannel.empty() && m.gChannel.empty() && m.bChannel.empty() && !channels.empty())
    {
        m.grayscale = true;
        m.soloChannel = channels[0];
    }

    return m;
}

bool ImageLoader::LoadEXRLayer(const std::wstring& filePath, const ExrLayer& layer, ImageData& outImage,
                               std::string& errorMsg)
{
    try
    {
        std::string narrowPath = WideToUtf8(filePath);
        Imf::MultiPartInputFile file(narrowPath.c_str());

        if (layer.partIndex < 0 || layer.partIndex >= file.parts())
        {
            errorMsg = "Invalid part index.";
            return false;
        }

        const Imf::Header& header = file.header(layer.partIndex);

        // Determine dimensions based on whether this is a tiled mip level
        int width, height;
        Imath::Box2i dw;

        if (layer.isTiled)
        {
            Imf::TiledInputPart tiledPart(file, layer.partIndex);
            dw = tiledPart.dataWindowForLevel(layer.mipLevel);
            width = dw.max.x - dw.min.x + 1;
            height = dw.max.y - dw.min.y + 1;
        }
        else
        {
            dw = header.dataWindow();
            width = dw.max.x - dw.min.x + 1;
            height = dw.max.y - dw.min.y + 1;
        }

        if (width <= 0 || height <= 0)
        {
            errorMsg = "Invalid image dimensions.";
            return false;
        }

        size_t pixelBytes = static_cast<size_t>(width) * height * 4 * sizeof(float);
        if (pixelBytes > kMaxPixelBytes)
        {
            errorMsg = "Image too large.";
            return false;
        }

        // Figure out which channels map to RGBA
        std::string prefix = layer.name.empty() ? "" : (layer.name + ".");
        ChannelMapping cm = MapChannelsToRGBA(layer.channels);
        std::string rName = cm.rChannel;
        std::string gName = cm.gChannel;
        std::string bName = cm.bChannel;
        std::string aName = cm.aChannel;
        bool grayscale = cm.grayscale;
        std::string soloChannel = cm.soloChannel;

        outImage.width = width;
        outImage.height = height;
        outImage.pixels.resize(static_cast<size_t>(width) * height * 4);

        // Fill with default (black, alpha=1)
        for (size_t i = 0; i < outImage.pixels.size(); i += 4)
        {
            outImage.pixels[i + 0] = 0.0f;
            outImage.pixels[i + 1] = 0.0f;
            outImage.pixels[i + 2] = 0.0f;
            outImage.pixels[i + 3] = 1.0f;
        }

        size_t xStride = 4 * sizeof(float);
        size_t yStride = static_cast<size_t>(width) * xStride;
        char* base = reinterpret_cast<char*>(outImage.pixels.data()) - dw.min.x * xStride -
                     static_cast<size_t>(dw.min.y) * yStride;

        Imf::FrameBuffer fb;

        if (grayscale)
        {
            std::string fullName = prefix + soloChannel;
            fb.insert(fullName.c_str(), Imf::Slice(Imf::FLOAT, base + 0, xStride, yStride));
        }
        else
        {
            if (!rName.empty())
                fb.insert((prefix + rName).c_str(), Imf::Slice(Imf::FLOAT, base + 0 * sizeof(float), xStride, yStride));
            if (!gName.empty())
                fb.insert((prefix + gName).c_str(), Imf::Slice(Imf::FLOAT, base + 1 * sizeof(float), xStride, yStride));
            if (!bName.empty())
                fb.insert((prefix + bName).c_str(), Imf::Slice(Imf::FLOAT, base + 2 * sizeof(float), xStride, yStride));
            if (!aName.empty())
                fb.insert((prefix + aName).c_str(), Imf::Slice(Imf::FLOAT, base + 3 * sizeof(float), xStride, yStride));
        }

        if (layer.isTiled)
        {
            Imf::TiledInputPart tiledPart(file, layer.partIndex);
            tiledPart.setFrameBuffer(fb);
            int lv = layer.mipLevel;
            tiledPart.readTiles(0, tiledPart.numXTiles(lv) - 1, 0, tiledPart.numYTiles(lv) - 1, lv);
        }
        else
        {
            Imf::InputPart part(file, layer.partIndex);
            part.setFrameBuffer(fb);
            part.readPixels(dw.min.y, dw.max.y);
        }

        // For grayscale, copy R to G and B; detect all-zero alpha in the same pass
        bool allZeroAlpha = true;
        for (size_t i = 0; i < outImage.pixels.size(); i += 4)
        {
            if (grayscale)
            {
                outImage.pixels[i + 1] = outImage.pixels[i + 0];
                outImage.pixels[i + 2] = outImage.pixels[i + 0];
            }
            allZeroAlpha = allZeroAlpha && (outImage.pixels[i + 3] == 0.0f);
        }
        outImage.alphaAllZero = allZeroAlpha;

        ComputeColorMatrix(header, outImage.colorMatrix, outImage.colorSpace);

        return true;
    }
    catch (const std::bad_alloc&)
    {
        errorMsg = "Not enough memory to load this image.";
        return false;
    }
    catch (const std::exception& e)
    {
        errorMsg = e.what();
        return false;
    }
    catch (...)
    {
        errorMsg = "Unknown error while reading layer.";
        return false;
    }
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty())
        return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

std::wstring FormatLayerLabel(const ExrLayer& layer)
{
    // Mip child levels
    if (layer.numMipLevels > 1 && layer.mipLevel > 0)
    {
        wchar_t buf[64];
        swprintf_s(buf, L"Mip %d  %d\u00D7%d", layer.mipLevel, layer.mipWidth, layer.mipHeight);
        return buf;
    }

    // Top-level layer
    std::wstring label;
    if (layer.name.empty())
        label = L"(default)";
    else
        label = Utf8ToWide(layer.name);

    // Append channel list
    label += L"  ";
    for (size_t i = 0; i < layer.channels.size(); i++)
    {
        if (i > 0)
            label += L",";
        label += Utf8ToWide(layer.channels[i]);
    }

    // Show dimensions for mip level 0 if mipmaps exist
    if (layer.numMipLevels > 1)
    {
        wchar_t buf[32];
        swprintf_s(buf, L"  %d\u00D7%d", layer.mipWidth, layer.mipHeight);
        label += buf;
    }

    return label;
}
