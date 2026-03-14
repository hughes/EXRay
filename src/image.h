// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ImageData
{
    int width = 0;
    int height = 0;
    std::vector<float> pixels; // RGBA interleaved, width*height*4 floats

    ImageData() = default;
    ImageData(const ImageData&) = default;
    ImageData& operator=(const ImageData&) = default;

    ImageData(ImageData&& o) noexcept : width(o.width), height(o.height), pixels(std::move(o.pixels))
    {
        o.width = 0;
        o.height = 0;
    }

    ImageData& operator=(ImageData&& o) noexcept
    {
        if (this != &o)
        {
            width = o.width;
            height = o.height;
            pixels = std::move(o.pixels);
            o.width = 0;
            o.height = 0;
        }
        return *this;
    }

    const float* PixelAt(int x, int y) const { return &pixels[static_cast<size_t>((y * width + x) * 4)]; }

    bool IsLoaded() const { return width > 0 && height > 0; }
};

// A layer within an EXR file (a group of channels sharing a prefix)
struct ExrLayer
{
    std::string name;                  // layer name ("" for root channels, "diffuse", "specular", etc.)
    std::vector<std::string> channels; // channel names within this layer (e.g. "R", "G", "B", "A")
    int partIndex = 0;                 // which part of a multi-part file this layer belongs to
    std::string partName;              // part name from the header (for multi-part files)
    bool isTiled = false;              // true if this part uses tiled storage
    int mipLevel = 0;                  // which mip level to load (0 = full res)
    int numMipLevels = 1;              // total mip levels available (1 = no mipmaps)
    int mipWidth = 0;                  // width at this mip level
    int mipHeight = 0;                 // height at this mip level
};

// Metadata about all layers/parts in an EXR file
struct ExrFileInfo
{
    std::vector<ExrLayer> layers;
    int partCount = 0;
};

class ImageLoader
{
  public:
    // Load default RGBA (backward compat)
    static bool LoadEXR(const std::wstring& filePath, ImageData& outImage, std::string& errorMsg);

    // Scan file for all layers/channels (metadata only, fast)
    static bool ScanLayers(const std::wstring& filePath, ExrFileInfo& outInfo, std::string& errorMsg);

    // Load a specific layer's channels into RGBA display
    static bool LoadEXRLayer(const std::wstring& filePath, const ExrLayer& layer, ImageData& outImage,
                             std::string& errorMsg);
};
