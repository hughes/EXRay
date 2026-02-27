#include "histogram.h"

#include "image.h"

#include <algorithm>
#include <cmath>

HistogramData HistogramComputer::Compute(const ImageData& image)
{
    HistogramData result;
    if (!image.IsLoaded())
        return result;

    const int N = image.width * image.height;
    constexpr int B = HistogramData::kBinCount;

    std::array<uint32_t, B> lumCounts{}, rCounts{}, gCounts{}, bCounts{};

    // First pass: find dynamic range
    float minPos = 1e30f, maxPos = 1e-30f;
    for (int i = 0; i < N; ++i)
    {
        const float* px = &image.pixels[static_cast<size_t>(i) * 4];
        float lum = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
        if (lum > 0.0f)
        {
            minPos = (std::min)(minPos, lum);
            maxPos = (std::max)(maxPos, lum);
        }
        for (int c = 0; c < 3; ++c)
        {
            if (px[c] > 0.0f)
            {
                minPos = (std::min)(minPos, px[c]);
                maxPos = (std::max)(maxPos, px[c]);
            }
        }
    }

    if (minPos >= maxPos)
        return result;

    result.log2Min = std::floor(std::log2((std::max)(minPos, 1e-10f)));
    result.log2Max = std::ceil(std::log2((std::max)(maxPos, 1e-10f)));
    if (result.log2Max <= result.log2Min)
        result.log2Max = result.log2Min + 1.0f;

    float range = result.log2Max - result.log2Min;
    float rangeInv = 1.0f / range;

    auto toBin = [&](float val) -> int
    {
        if (val <= 0.0f)
            return 0;
        float t = (std::log2(val) - result.log2Min) * rangeInv;
        int bin = static_cast<int>(t * (B - 1));
        return (std::max)(0, (std::min)(bin, B - 1));
    };

    // Second pass: bin all pixels
    for (int i = 0; i < N; ++i)
    {
        const float* px = &image.pixels[static_cast<size_t>(i) * 4];
        float lum = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];

        lumCounts[toBin(lum)]++;
        rCounts[toBin(px[0])]++;
        gCounts[toBin(px[1])]++;
        bCounts[toBin(px[2])]++;
    }

    // Normalize each channel to [0,1] using log scale to handle spiky distributions
    auto normalize = [](const std::array<uint32_t, B>& counts, std::array<float, B>& out)
    {
        uint32_t maxCount = *std::max_element(counts.begin(), counts.end());
        if (maxCount == 0)
            return;
        float logMax = std::log(1.0f + static_cast<float>(maxCount));
        float inv = 1.0f / logMax;
        for (int i = 0; i < B; ++i)
            out[i] = std::log(1.0f + static_cast<float>(counts[i])) * inv;
    };

    normalize(lumCounts, result.luminance);
    normalize(rCounts, result.red);
    normalize(gCounts, result.green);
    normalize(bCounts, result.blue);

    result.isValid = true;
    return result;
}
