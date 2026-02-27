#pragma once

#include <array>

struct ImageData;

struct HistogramData
{
    static constexpr int kBinCount = 512;

    std::array<float, kBinCount> luminance{};
    std::array<float, kBinCount> red{};
    std::array<float, kBinCount> green{};
    std::array<float, kBinCount> blue{};

    float log2Min = -10.0f;
    float log2Max = 10.0f;
    bool isValid = false;
};

class HistogramComputer
{
  public:
    static HistogramData Compute(const ImageData& image);
};
