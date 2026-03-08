// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include "histogram.h"
#include "image.h"

#include <functional>
#include <windows.h>

class Sidebar
{
  public:
    using ExposureChangeHandler = std::function<void(float newExposure)>;
    using GammaChangeHandler = std::function<void(float newGamma)>;
    using AutoExposureHandler = std::function<void()>;
    using HistogramChannelHandler = std::function<void(int channel)>;
    using LayerSelectHandler = std::function<void(int layerIndex)>;

    bool Create(HWND parent, HINSTANCE hInstance);

    void SetVisible(bool visible);
    bool IsVisible() const { return m_visible; }
    HWND GetHwnd() const { return m_hwnd; }
    int GetWidth() const;

    // Update display data (does not take ownership — copies what it needs)
    void SetHistogramData(const HistogramData& data, int channelMode);
    void SetExposureGamma(float exposure, float gamma, bool isHDR);

    // Layer browser
    void SetLayers(const ExrFileInfo& info, int activeLayer);

    // Callbacks
    ExposureChangeHandler onExposureChange;
    GammaChangeHandler onGammaChange;
    AutoExposureHandler onAutoExposure;
    HistogramChannelHandler onHistogramChannel;
    LayerSelectHandler onLayerSelect;

  private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnPaint();
    void LayoutControls();

    HWND m_hwnd = nullptr;
    HWND m_parent = nullptr;
    bool m_visible = false;

    // Exposure/gamma trackbars
    HWND m_exposureTrack = nullptr;
    HWND m_gammaTrack = nullptr;
    HWND m_autoExpButton = nullptr;

    // Histogram channel buttons
    HWND m_channelCombo = nullptr;

    // Layer browser
    HWND m_layerList = nullptr;
    ExrFileInfo m_layerInfo;
    int m_activeLayer = 0;
    bool m_suppressLayerChange = false;
    std::vector<int> m_layerListMapping; // listbox index → ExrLayer index (skipping headers)

    // Current display state
    HistogramData m_histogram;
    int m_channelMode = 4; // 0=Lum, 1=R, 2=G, 3=B, 4=All
    float m_exposure = 0.0f;
    float m_gamma = 1.0f / 2.2f;
    bool m_isHDR = false;

    // Suppress feedback loops when setting trackbar position programmatically
    bool m_suppressTrackbar = false;

    static constexpr int kBaseWidth = 240;
    static constexpr int kHistogramHeight = 100;
    static constexpr int kMargin = 8;

    // Control IDs
    static constexpr int kExposureTrackId = 2001;
    static constexpr int kGammaTrackId = 2002;
    static constexpr int kAutoExpButtonId = 2003;
    static constexpr int kChannelComboId = 2004;
    static constexpr int kLayerListId = 2005;
};
