// SPDX-License-Identifier: GPL-3.0-or-later

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

    // First pass: find dynamic range (skip NaN, Inf, negative, zero)
    float minPos = 1e30f, maxPos = 1e-30f;
    for (int i = 0; i < N; ++i)
    {
        const float* px = &image.pixels[static_cast<size_t>(i) * 4];
        float lum = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
        if (lum > 0.0f && std::isfinite(lum))
        {
            minPos = (std::min)(minPos, lum);
            maxPos = (std::max)(maxPos, lum);
        }
        for (int c = 0; c < 3; ++c)
        {
            if (px[c] > 0.0f && std::isfinite(px[c]))
            {
                minPos = (std::min)(minPos, px[c]);
                maxPos = (std::max)(maxPos, px[c]);
            }
        }
    }

    if (minPos > maxPos)
        return result;

    // Fixed range so the histogram axis is stable across all images.
    // Asymmetric to keep crush visible (~7%) and blowout narrow (~20%).
    result.log2Min = -16.0f;
    result.log2Max = 4.0f;

    float range = result.log2Max - result.log2Min;
    float rangeInv = 1.0f / range;

    auto toBin = [&](float val) -> int
    {
        if (val <= 0.0f || !std::isfinite(val))
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

    // Auto-exposure: find 97th percentile luminance and set EV so it maps to 1.0.
    // This keeps almost all content visible without clipping, while leaving
    // images already in [0,1] range essentially unchanged (exposure ≈ 0).
    {
        uint32_t totalLum = 0;
        for (int i = 0; i < B; ++i)
            totalLum += lumCounts[i];

        if (totalLum > 0)
        {
            uint32_t p97Target = totalLum * 97 / 100;
            uint32_t cumulative = 0;
            for (int i = 0; i < B; ++i)
            {
                cumulative += lumCounts[i];
                if (cumulative >= p97Target)
                {
                    float p97Log2 = result.log2Min + (static_cast<float>(i) / (B - 1)) * range;
                    result.autoExposure = -p97Log2;
                    break;
                }
            }
        }
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
