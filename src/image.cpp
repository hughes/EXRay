#ifndef UNICODE
#define UNICODE
#endif

#include "image.h"

#include <ImfArray.h>
#include <ImfRgbaFile.h>
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

bool ImageLoader::LoadEXR(const std::wstring& filePath, ImageData& outImage, std::string& errorMsg)
{
    try
    {
        std::string narrowPath = WideToUtf8(filePath);

        Imf::RgbaInputFile file(narrowPath.c_str());
        Imath::Box2i dw = file.dataWindow();

        int width = dw.max.x - dw.min.x + 1;
        int height = dw.max.y - dw.min.y + 1;

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

        return true;
    }
    catch (const std::exception& e)
    {
        errorMsg = e.what();
        return false;
    }
}
