// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

struct ImageData;

struct HistogramData
{
    static constexpr int kBinCount = 300; // matches panel width for 1:1 pixel mapping

    std::array<float, kBinCount> luminance{};
    std::array<float, kBinCount> red{};
    std::array<float, kBinCount> green{};
    std::array<float, kBinCount> blue{};

    float log2Min = -16.0f;
    float log2Max = 4.0f;
    float autoExposure = 0.0f; // EV to bring 97th-percentile luminance to 1.0
    bool isValid = false;
};

class HistogramComputer
{
  public:
    static HistogramData Compute(const ImageData& image);
};
