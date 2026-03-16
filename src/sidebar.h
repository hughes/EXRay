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
    using CompareActionHandler = std::function<void()>;
    using CompareModeHandler = std::function<void(int mode)>; // 0=off, 1=split, 2=diff

    bool Create(HWND parent, HINSTANCE hInstance);

    void SetVisible(bool visible);
    bool IsVisible() const { return m_visible; }
    HWND GetHwnd() const { return m_hwnd; }
    int GetWidth() const;

    // Enable/disable all interactive controls (for when no image is loaded)
    void SetEnabled(bool enabled);

    // Update display data (does not take ownership — copies what it needs)
    void SetHistogramData(const HistogramData& data, int channelMode);
    void SetExposureGamma(float exposure, float gamma, bool isHDR);
    void SetFont(HFONT font);

    // Re-apply theme to all child controls (call after Theme::Refresh())
    void RefreshTheme();

    // Layer browser
    void SetLayers(const ExrFileInfo& info, int activeLayer);

    // Compare panel
    void SetCompareState(int mode, const wchar_t* labelA, const wchar_t* labelB, int focusedSource);
    void ClearCompareState();

    // Sequence panel
    void SetSequenceState(int currentFrame, int totalFrames);
    void ClearSequenceState();

    // Callbacks
    ExposureChangeHandler onExposureChange;
    GammaChangeHandler onGammaChange;
    AutoExposureHandler onAutoExposure;
    HistogramChannelHandler onHistogramChannel;
    LayerSelectHandler onLayerSelect;
    CompareModeHandler onCompareMode;
    CompareActionHandler onCompareSwap;
    CompareActionHandler onCompareFocusA;
    CompareActionHandler onCompareFocusB;

    // Sequence callbacks
    using SequenceNavHandler = std::function<void(int delta)>; // +1/-1 for next/prev
    using SequenceJumpHandler = std::function<void(int frameNumber)>;
    SequenceNavHandler onSequenceNav;
    CompareActionHandler onSequenceFirst;
    CompareActionHandler onSequenceLast;
    SequenceJumpHandler onSequenceJump;

  private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnPaint();
    void LayoutControls();

    HWND m_hwnd = nullptr;
    HWND m_parent = nullptr;
    bool m_visible = false;
    bool m_enabled = false;

    // Exposure/gamma trackbars
    HWND m_exposureTrack = nullptr;
    HWND m_gammaTrack = nullptr;
    HWND m_autoExpButton = nullptr;

    // Histogram channel buttons (L, R, G, B, All)
    HWND m_channelButtons[5] = {};

    // Layer browser
    HWND m_layerList = nullptr;
    ExrFileInfo m_layerInfo;
    int m_activeLayer = 0;
    bool m_suppressLayerChange = false;
    std::vector<int> m_layerListMapping; // listbox index → ExrLayer index (skipping headers)

    // Compare panel
    static constexpr int kNumCompareModes = 5; // None, Split, Diff, Add, Over
    HWND m_compareModeBtns[kNumCompareModes] = {};
    HWND m_compareFocusBtns[2] = {};  // [Focus Left/Base] [Focus Right/Blend]
    HWND m_compareSwapBtn = nullptr;  // [Swap]
    int m_compareMode = 0;            // 0=off, 1=split, 2=diff, 3=add, 4=over
    int m_compareFocus = 0;           // 0=base/left, 1=blend/right
    std::wstring m_compareLabelBase;
    std::wstring m_compareLabelBlend;

    // Current display state
    HistogramData m_histogram;
    int m_channelMode = 4; // 0=Lum, 1=R, 2=G, 3=B, 4=All
    float m_exposure = 0.0f;
    float m_gamma = 1.0f / 2.2f;
    bool m_isHDR = false;

    // Suppress feedback loops when setting trackbar position programmatically
    bool m_suppressTrackbar = false;

    // Font (not owned — managed by Window, passed via SetFont)
    HFONT m_font = nullptr;

    static constexpr int kBaseWidth = 240;
    static constexpr int kHistogramHeight = 100;
    static constexpr int kMargin = 8;

    // Control IDs
    static constexpr int kExposureTrackId = 2001;
    static constexpr int kGammaTrackId = 2002;
    static constexpr int kAutoExpButtonId = 2003;
    static constexpr int kChannelBtnBaseId = 2004; // 2004..2008 for 5 buttons
    static constexpr int kLayerListId = 2010;
    static constexpr int kCompareModeBtnBaseId = 2020;   // 2020..2024 (None, Split, Diff, Add, Over)
    static constexpr int kCompareFocusBtnBaseId = 2025;  // 2025..2026
    static constexpr int kCompareActionBtnBaseId = 2030; // 2030..2032

    // Sequence controls
    static constexpr int kSeqFirstBtnId = 2040;
    static constexpr int kSeqPrevBtnId = 2041;
    static constexpr int kSeqNextBtnId = 2042;
    static constexpr int kSeqLastBtnId = 2043;
    static constexpr int kSeqFrameEditId = 2044;

    HWND m_seqFirstBtn = nullptr;
    HWND m_seqPrevBtn = nullptr;
    HWND m_seqNextBtn = nullptr;
    HWND m_seqLastBtn = nullptr;
    HWND m_seqFrameEdit = nullptr;
    int m_seqCurrentFrame = 0;
    int m_seqTotalFrames = 0;
    bool m_seqActive = false;
    int m_seqRepeatDelta = 0;      // +1 or -1 while holding nav button
    static constexpr UINT_PTR kSeqRepeatTimerId = 100;
    static constexpr int kSeqRepeatInitialMs = 300;
    static constexpr int kSeqRepeatMs = 80;
};
