// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "image.h"

#include <ImfArray.h>
#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfInputPart.h>
#include <ImfMultiPartInputFile.h>
#include <ImfRgbaFile.h>
#include <ImfTileDescription.h>
#include <ImfTiledInputPart.h>
#include <algorithm>
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
            }
        }

        // Detect all-zero alpha
        bool allZero = true;
        for (size_t i = 3; i < outImage.pixels.size() && allZero; i += 4)
            allZero = (outImage.pixels[i] == 0.0f);
        outImage.alphaAllZero = allZero;

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

        // Try to map channels to RGBA display
        std::string rName = FindChannel(layer.channels, {"R", "r", "red", "Red", "x", "X"});
        std::string gName = FindChannel(layer.channels, {"G", "g", "green", "Green", "y", "Y"});
        std::string bName = FindChannel(layer.channels, {"B", "b", "blue", "Blue", "z", "Z"});
        std::string aName = FindChannel(layer.channels, {"A", "a", "alpha", "Alpha"});

        // If only 1 channel, display as grayscale
        bool grayscale = false;
        std::string soloChannel;
        if (rName.empty() && gName.empty() && bName.empty())
        {
            if (layer.channels.size() == 1)
            {
                grayscale = true;
                soloChannel = layer.channels[0];
            }
            else if (!layer.channels.empty())
            {
                grayscale = true;
                soloChannel = layer.channels[0];
            }
        }

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

        // For grayscale, copy R to G and B
        if (grayscale)
        {
            for (size_t i = 0; i < outImage.pixels.size(); i += 4)
            {
                outImage.pixels[i + 1] = outImage.pixels[i + 0];
                outImage.pixels[i + 2] = outImage.pixels[i + 0];
            }
        }

        // Detect all-zero alpha
        bool allZero = true;
        for (size_t i = 3; i < outImage.pixels.size() && allZero; i += 4)
            allZero = (outImage.pixels[i] == 0.0f);
        outImage.alphaAllZero = allZero;

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
