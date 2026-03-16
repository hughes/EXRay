// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct ImageData
{
    int width = 0;
    int height = 0;
    bool alphaAllZero = false; // true if every alpha sample was exactly 0
    std::vector<float> pixels; // RGBA interleaved, width*height*4 floats

    // 3x3 row-major color matrix: source primaries → Rec. 709
    // Identity by default (no conversion needed for Rec. 709 sources)
    float colorMatrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::string colorSpace; // empty = Rec. 709 (default), otherwise e.g. "ACEScg", "DCI-P3"

    ImageData() = default;
    ImageData(const ImageData&) = default;
    ImageData& operator=(const ImageData&) = default;

    ImageData(ImageData&& o) noexcept
        : width(o.width), height(o.height), alphaAllZero(o.alphaAllZero), pixels(std::move(o.pixels)),
          colorSpace(std::move(o.colorSpace))
    {
        std::memcpy(colorMatrix, o.colorMatrix, sizeof(colorMatrix));
        o.width = 0;
        o.height = 0;
        o.alphaAllZero = false;
    }

    ImageData& operator=(ImageData&& o) noexcept
    {
        if (this != &o)
        {
            width = o.width;
            height = o.height;
            alphaAllZero = o.alphaAllZero;
            pixels = std::move(o.pixels);
            std::memcpy(colorMatrix, o.colorMatrix, sizeof(colorMatrix));
            colorSpace = std::move(o.colorSpace);
            o.width = 0;
            o.height = 0;
            o.alphaAllZero = false;
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

// Result of mapping layer channel names to RGBA display slots
struct ChannelMapping
{
    std::string rChannel;    // channel name mapped to red (empty = none)
    std::string gChannel;    // channel name mapped to green
    std::string bChannel;    // channel name mapped to blue
    std::string aChannel;    // channel name mapped to alpha
    bool grayscale = false;  // true if single channel displayed as gray
    std::string soloChannel; // channel name used for grayscale
};

// Map a layer's channel names to RGBA display slots.
// Rules: RGB/rgba names first, then XYZ only as a complete triplet,
// then fall back to grayscale for single-channel layers.
ChannelMapping MapChannelsToRGBA(const std::vector<std::string>& channels);

// Format a layer label for display (used in sidebar layer list and compare source labels).
// Returns e.g. "(default)  Y", "right  Y", "Mip 2  128x128"
std::wstring FormatLayerLabel(const ExrLayer& layer);

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
