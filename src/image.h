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

class ImageLoader
{
  public:
    static bool LoadEXR(const std::wstring& filePath, ImageData& outImage, std::string& errorMsg);
};
