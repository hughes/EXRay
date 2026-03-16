// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "sidebar.h"

#include "themes.h"

#include <algorithm>
#include <cmath>
#include <commctrl.h>
#include <cstdio>

static const wchar_t* const kSidebarClassName = L"EXRay_Sidebar";

// Exposure trackbar: range -80 to +80 (maps to -20.0 to +20.0 EV in 0.25 steps)
static constexpr int kExpTrackMin = -80;
static constexpr int kExpTrackMax = 80;
static float ExposureFromTrackPos(int pos) { return static_cast<float>(pos) * 0.25f; }
static int TrackPosFromExposure(float ev) { return static_cast<int>(ev * 4.0f); }

// Gamma trackbar: range 25 to 100 (maps to gamma exponent 0.25 to 1.0 in 0.05 steps)
static constexpr int kGammaTrackMin = 5;
static constexpr int kGammaTrackMax = 20;
static float GammaFromTrackPos(int pos) { return static_cast<float>(pos) * 0.05f; }
static int TrackPosFromGamma(float g) { return static_cast<int>(g * 20.0f); }

static COLORREF ChannelColor(int ch)
{
    switch (ch)
    {
    case 0:
        return RGB(200, 200, 200); // Luminance
    case 1:
        return RGB(220, 60, 60); // Red
    case 2:
        return RGB(60, 200, 60); // Green
    case 3:
        return RGB(60, 100, 220); // Blue
    default:
        return RGB(200, 200, 200);
    }
}

bool Sidebar::Create(HWND parent, HINSTANCE hInstance)
{
    m_parent = parent;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kSidebarClassName;
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(WS_EX_COMPOSITED, kSidebarClassName, nullptr, WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0,
                             parent, nullptr, hInstance, this);
    if (!m_hwnd)
        return false;

    // Use a default font until SetFont is called
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    // Channel selector buttons (L, R, G, B, All)
    {
        const wchar_t* labels[] = {L"L", L"R", L"G", L"B", L"All"};
        for (int i = 0; i < 5; i++)
        {
            m_channelButtons[i] = CreateWindowExW(
                0, WC_BUTTONW, labels[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, m_hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChannelBtnBaseId + i)), hInstance, nullptr);
        }
    }

    // Exposure trackbar
    m_exposureTrack =
        CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 0, 0, 0, 0, m_hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExposureTrackId)), hInstance, nullptr);
    SendMessageW(m_exposureTrack, TBM_SETRANGEMIN, FALSE, kExpTrackMin);
    SendMessageW(m_exposureTrack, TBM_SETRANGEMAX, FALSE, kExpTrackMax);
    SendMessageW(m_exposureTrack, TBM_SETPOS, TRUE, 0);
    SendMessageW(m_exposureTrack, TBM_SETLINESIZE, 0, 1); // arrow keys = 0.25 EV
    SendMessageW(m_exposureTrack, TBM_SETPAGESIZE, 0, 4); // page = 1.0 EV
    Theme::ApplyToControl(m_exposureTrack);

    // Auto-exposure button (owner-drawn for dark theme)
    m_autoExpButton =
        CreateWindowExW(0, WC_BUTTONW, L"Auto", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, m_hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutoExpButtonId)), hInstance, nullptr);

    // Gamma trackbar
    m_gammaTrack =
        CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 0, 0, 0, 0, m_hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGammaTrackId)), hInstance, nullptr);
    SendMessageW(m_gammaTrack, TBM_SETRANGEMIN, FALSE, kGammaTrackMin);
    SendMessageW(m_gammaTrack, TBM_SETRANGEMAX, FALSE, kGammaTrackMax);
    SendMessageW(m_gammaTrack, TBM_SETPOS, TRUE, TrackPosFromGamma(m_gamma));
    SendMessageW(m_gammaTrack, TBM_SETLINESIZE, 0, 1); // arrow keys = 0.05
    SendMessageW(m_gammaTrack, TBM_SETPAGESIZE, 0, 2); // page = 0.10
    Theme::ApplyToControl(m_gammaTrack);

    // Compare mode buttons (always visible — None/Split/Diff/Add/Over)
    {
        const wchar_t* modeLabels[] = {L"None", L"Split", L"Diff", L"Add", L"Over"};
        for (int i = 0; i < kNumCompareModes; i++)
        {
            m_compareModeBtns[i] = CreateWindowExW(
                0, WC_BUTTONW, modeLabels[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, m_hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCompareModeBtnBaseId + i)), hInstance, nullptr);
        }
    }
    // Compare focus buttons (hidden until compare active)
    {
        const wchar_t* focusLabels[] = {L"Focus L", L"Focus R"};
        for (int i = 0; i < 2; i++)
        {
            m_compareFocusBtns[i] = CreateWindowExW(
                0, WC_BUTTONW, focusLabels[i], WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, m_hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCompareFocusBtnBaseId + i)), hInstance, nullptr);
        }
    }
    // Compare swap button (hidden until compare active)
    m_compareSwapBtn = CreateWindowExW(
        0, WC_BUTTONW, L"Swap", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, m_hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCompareActionBtnBaseId)), hInstance, nullptr);

    // Layer listbox (hidden until layers are set) — owner-draw for hierarchical indentation
    m_layerList = CreateWindowExW(
        0, WC_LISTBOXW, nullptr,
        WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0, 0, 0, 0,
        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLayerListId)), hInstance, nullptr);
    SendMessageW(m_layerList, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    Theme::ApplyToControl(m_layerList);

    return true;
}

void Sidebar::SetFont(HFONT font)
{
    m_font = font;
    for (auto btn : m_channelButtons)
        if (btn)
            SendMessageW(btn, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (m_autoExpButton)
        SendMessageW(m_autoExpButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (m_layerList)
        SendMessageW(m_layerList, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Sidebar::RefreshTheme()
{
    Theme::ApplyToControl(m_exposureTrack);
    Theme::ApplyToControl(m_gammaTrack);
    Theme::ApplyToControl(m_layerList);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

int Sidebar::GetWidth() const
{
    if (!m_visible || !m_hwnd)
        return 0;
    int dpi = GetDpiForWindow(m_hwnd);
    return MulDiv(kBaseWidth, dpi, 96);
}

void Sidebar::SetVisible(bool visible)
{
    m_visible = visible;
    ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
}

void Sidebar::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    EnableWindow(m_exposureTrack, enabled);
    EnableWindow(m_gammaTrack, enabled);
    EnableWindow(m_autoExpButton, enabled);
    for (auto btn : m_channelButtons)
        EnableWindow(btn, enabled);
    EnableWindow(m_layerList, enabled);
    for (auto btn : m_compareModeBtns)
        EnableWindow(btn, enabled);
    for (auto btn : m_compareFocusBtns)
        EnableWindow(btn, enabled);
    if (m_compareSwapBtn)
        EnableWindow(m_compareSwapBtn, enabled);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Sidebar::SetHistogramData(const HistogramData& data, int channelMode)
{
    m_histogram = data;
    m_channelMode = channelMode;
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Sidebar::SetExposureGamma(float exposure, float gamma, bool isHDR)
{
    bool hdrChanged = (isHDR != m_isHDR);
    m_exposure = exposure;
    m_gamma = gamma;
    m_isHDR = isHDR;

    m_suppressTrackbar = true;
    SendMessageW(m_exposureTrack, TBM_SETPOS, TRUE, TrackPosFromExposure(exposure));
    SendMessageW(m_gammaTrack, TBM_SETPOS, TRUE, TrackPosFromGamma(gamma));
    m_suppressTrackbar = false;

    if (hdrChanged)
    {
        ShowWindow(m_gammaTrack, isHDR ? SW_HIDE : SW_SHOW);
        LayoutControls();
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Sidebar::SetCompareState(int mode, const wchar_t* labelBase, const wchar_t* labelBlend, int focusedSource)
{
    bool modeChanged = (mode != m_compareMode);
    m_compareLabelBase = labelBase ? labelBase : L"";
    m_compareLabelBlend = labelBlend ? labelBlend : L"";
    m_compareFocus = focusedSource;

    if (modeChanged)
    {
        m_compareMode = mode;
        bool show = (mode > 0);
        for (auto btn : m_compareFocusBtns)
            ShowWindow(btn, show ? SW_SHOW : SW_HIDE);
        ShowWindow(m_compareSwapBtn, show ? SW_SHOW : SW_HIDE);

        bool isSplit = (mode == 1);
        SetWindowTextW(m_compareFocusBtns[0], isSplit ? L"Focus L" : L"Focus Base");
        SetWindowTextW(m_compareFocusBtns[1], isSplit ? L"Focus R" : L"Focus Blend");

        for (auto btn : m_compareModeBtns)
            InvalidateRect(btn, nullptr, FALSE);

        LayoutControls();
    }

    // Always invalidate focus buttons (focus may have changed) and labels
    for (auto btn : m_compareFocusBtns)
        InvalidateRect(btn, nullptr, FALSE);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Sidebar::ClearCompareState()
{
    SetCompareState(0, nullptr, nullptr, 0);
}

void Sidebar::SetLayers(const ExrFileInfo& info, int activeLayer)
{
    m_layerInfo = info;
    m_activeLayer = activeLayer;

    m_suppressLayerChange = true;
    SendMessageW(m_layerList, LB_RESETCONTENT, 0, 0);

    bool hasLayers = info.layers.size() > 1;
    ShowWindow(m_layerList, hasLayers ? SW_SHOW : SW_HIDE);

    if (hasLayers)
    {
        // Helper to convert UTF-8 to wide string
        auto toWide = [](const std::string& s) -> std::wstring
        {
            if (s.empty())
                return {};
            int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
            std::wstring w(len - 1, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
            return w;
        };

        bool multiPart = info.partCount > 1;

        for (int idx = 0; idx < static_cast<int>(info.layers.size()); idx++)
        {
            const auto& layer = info.layers[idx];
            std::wstring label;
            int indent = 0;

            if (layer.numMipLevels > 1 && layer.mipLevel > 0)
            {
                indent = multiPart ? 2 : 1;
            }
            else if (multiPart)
            {
                indent = 1;
                bool newPart = (idx == 0) || (info.layers[idx - 1].partIndex != layer.partIndex);
                if (newPart)
                {
                    std::wstring partLabel;
                    if (!layer.partName.empty())
                        partLabel = toWide(layer.partName);
                    else
                    {
                        wchar_t buf[16];
                        swprintf_s(buf, L"Part %d", layer.partIndex);
                        partLabel = buf;
                    }
                    int partIdx = static_cast<int>(
                        SendMessageW(m_layerList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(partLabel.c_str())));
                    SendMessageW(m_layerList, LB_SETITEMDATA, partIdx, 0x80000000);
                }
            }

            label = FormatLayerLabel(layer);

            int listIdx =
                static_cast<int>(SendMessageW(m_layerList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
            // Store indent level in item data (low bits = indent, high bit = header flag)
            SendMessageW(m_layerList, LB_SETITEMDATA, listIdx, indent);
        }

        // Map activeLayer (ExrLayer index) to listbox index (which may have header items)
        m_layerListMapping.clear();
        int itemCount = static_cast<int>(SendMessageW(m_layerList, LB_GETCOUNT, 0, 0));
        for (int i = 0; i < itemCount; i++)
        {
            LRESULT data = SendMessageW(m_layerList, LB_GETITEMDATA, i, 0);
            if (!(data & 0x80000000)) // not a header
                m_layerListMapping.push_back(i);
        }

        if (activeLayer >= 0 && activeLayer < static_cast<int>(m_layerListMapping.size()))
            SendMessageW(m_layerList, LB_SETCURSEL, m_layerListMapping[activeLayer], 0);
    }

    m_suppressLayerChange = false;
    LayoutControls();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Sidebar::LayoutControls()
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int w = rc.right - rc.left;
    int dpi = GetDpiForWindow(m_hwnd);
    int m = MulDiv(kMargin, dpi, 96);
    int labelH = MulDiv(16, dpi, 96);
    int trackH = MulDiv(24, dpi, 96);
    int buttonW = MulDiv(44, dpi, 96);
    int buttonH = MulDiv(22, dpi, 96);
    int channelBtnH = MulDiv(22, dpi, 96);
    int histH = MulDiv(kHistogramHeight, dpi, 96);

    int y = m;

    // Histogram area is painted in OnPaint — just skip past it
    y += histH + MulDiv(4, dpi, 96);

    // 5 channel + 5 mode + 2 focus + 1 swap + exposure + auto + gamma + layerlist = 17
    HDWP hdwp = BeginDeferWindowPos(17);

    // Channel buttons — adjacent, forming a single button group
    {
        int totalW = w - 2 * m;
        int btnW = totalW / 5;
        int x = m;
        for (int i = 0; i < 5; i++)
        {
            int bw = (i == 4) ? (m + totalW - x) : btnW;
            hdwp = DeferWindowPos(hdwp, m_channelButtons[i], nullptr, x, y, bw, channelBtnH,
                                  SWP_NOZORDER | SWP_NOACTIVATE);
            x += bw;
        }
    }
    y += channelBtnH + m;

    // --- Compare mode buttons (always visible) ---
    // "Compare" section label drawn in OnPaint
    y += labelH + MulDiv(2, dpi, 96);

    // Mode button group: [None][Split][Diff][Add][Over]
    {
        int totalW = w - 2 * m;
        int btnW = totalW / kNumCompareModes;
        int x = m;
        for (int i = 0; i < kNumCompareModes; i++)
        {
            int bw = (i == kNumCompareModes - 1) ? (m + totalW - x) : btnW;
            hdwp = DeferWindowPos(hdwp, m_compareModeBtns[i], nullptr, x, y, bw, channelBtnH,
                                  SWP_NOZORDER | SWP_NOACTIVATE);
            x += bw;
        }
    }
    y += channelBtnH + m;

    // --- Compare detail panel (only when compare mode is active) ---
    if (m_compareMode > 0)
    {
        // Source base/left label drawn in OnPaint
        y += labelH;
        // Source blend/right label drawn in OnPaint
        y += labelH + MulDiv(4, dpi, 96);

        // Control row: [Focus L/Base] [Focus R/Blend] [Swap]
        {
            int totalW = w - 2 * m;
            int btnW = totalW / 3;
            int x = m;
            for (int i = 0; i < 2; i++)
            {
                hdwp = DeferWindowPos(hdwp, m_compareFocusBtns[i], nullptr, x, y, btnW, buttonH,
                                      SWP_NOZORDER | SWP_NOACTIVATE);
                x += btnW;
            }
            hdwp = DeferWindowPos(hdwp, m_compareSwapBtn, nullptr, x, y, m + totalW - x, buttonH,
                                  SWP_NOZORDER | SWP_NOACTIVATE);
        }
        y += buttonH + m;
    }

    // Exposure label is drawn in OnPaint
    y += labelH + MulDiv(2, dpi, 96);

    // Exposure trackbar + auto button
    int trackW = w - 2 * m - buttonW - MulDiv(4, dpi, 96);
    hdwp = DeferWindowPos(hdwp, m_exposureTrack, nullptr, m, y, trackW, trackH, SWP_NOZORDER | SWP_NOACTIVATE);
    hdwp = DeferWindowPos(hdwp, m_autoExpButton, nullptr, m + trackW + MulDiv(4, dpi, 96), y + (trackH - buttonH) / 2,
                          buttonW, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
    y += trackH + m;

    // Gamma section — only in SDR mode
    if (!m_isHDR)
    {
        // Gamma label drawn in OnPaint
        y += labelH + MulDiv(2, dpi, 96);

        // Gamma trackbar
        hdwp = DeferWindowPos(hdwp, m_gammaTrack, nullptr, m, y, w - 2 * m, trackH, SWP_NOZORDER | SWP_NOACTIVATE);
        y += trackH + m;
    }

    // Layer browser — only when multiple layers exist
    if (m_layerInfo.layers.size() > 1)
    {
        // "Layers" label drawn in OnPaint
        y += labelH + MulDiv(2, dpi, 96);

        // Fill remaining vertical space
        int totalH = rc.bottom - rc.top;
        int listH = totalH - y - m;
        if (listH < MulDiv(36, dpi, 96))
            listH = MulDiv(36, dpi, 96);
        hdwp = DeferWindowPos(hdwp, m_layerList, nullptr, m, y, w - 2 * m, listH, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    EndDeferWindowPos(hdwp);
}

void Sidebar::OnPaint()
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int dpi = GetDpiForWindow(m_hwnd);
    int m = MulDiv(kMargin, dpi, 96);
    int labelH = MulDiv(16, dpi, 96);
    int histH = MulDiv(kHistogramHeight, dpi, 96);

    // Double-buffer to avoid flicker
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDC, memBmp));

    // Background
    HBRUSH bgBrush = CreateSolidBrush(Colors::Background);
    FillRect(memDC, &rc, bgBrush);
    DeleteObject(bgBrush);

    // Separator line on left edge
    HBRUSH sepBrush = CreateSolidBrush(Colors::Separator);
    RECT sepRect = {0, 0, 1, h};
    FillRect(memDC, &sepRect, sepBrush);
    DeleteObject(sepBrush);

    SetBkMode(memDC, TRANSPARENT);
    COLORREF labelColor = m_enabled ? Colors::TextPrimary : Colors::TextDim;
    SetTextColor(memDC, labelColor);
    HFONT font = m_font ? m_font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SelectObject(memDC, font);

    int y = m;

    // --- Histogram section ---
    // Histogram background
    RECT histRect = {m, y, w - m, y + histH};
    HBRUSH histBg = CreateSolidBrush(Colors::Surface);
    FillRect(memDC, &histRect, histBg);
    DeleteObject(histBg);

    // Draw histogram in output space (post-exposure log2)
    {
        int barAreaW = histRect.right - histRect.left;
        int barAreaH = histRect.bottom - histRect.top;

        if (barAreaW > 0 && barAreaH > 0)
        {
            constexpr float kDispMin = -10.0f;
            constexpr float kDispMax = 4.0f;
            constexpr float kDispRange = kDispMax - kDispMin;

            constexpr float kClipLog2 = 0.0f;
            constexpr float kCrushLog2 = -6.644f;

            float clipT = (kClipLog2 - kDispMin) / kDispRange;
            float crushT = (kCrushLog2 - kDispMin) / kDispRange;

            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
            bmi.bmiHeader.biWidth = barAreaW;
            bmi.bmiHeader.biHeight = barAreaH; // bottom-up
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            uint32_t* pixels = nullptr;
            HBITMAP histBmp =
                CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
            if (histBmp && pixels)
            {
                int clipPx = static_cast<int>(clipT * barAreaW);
                int crushPx = static_cast<int>(crushT * barAreaW);
                if (clipPx < 0)
                    clipPx = 0;
                if (clipPx > barAreaW)
                    clipPx = barAreaW;
                if (crushPx < 0)
                    crushPx = 0;
                if (crushPx > barAreaW)
                    crushPx = barAreaW;

                // Histogram zone backgrounds (BGR pixel order for DIB)
                COLORREF surf = Colors::Surface;
                uint32_t bgNormal = GetRValue(surf) | (GetGValue(surf) << 8) | (GetBValue(surf) << 16);
                // Crush zone slightly darker, clip zone slightly lighter
                auto darken = [](uint32_t c, int amt) -> uint32_t
                {
                    int b = (std::max)(static_cast<int>(c & 0xFF) - amt, 0);
                    int g = (std::max)(static_cast<int>((c >> 8) & 0xFF) - amt, 0);
                    int r = (std::max)(static_cast<int>((c >> 16) & 0xFF) - amt, 0);
                    return static_cast<uint32_t>(b | (g << 8) | (r << 16));
                };
                auto lighten = [](uint32_t c, int amt) -> uint32_t
                {
                    int b = (std::min)(static_cast<int>(c & 0xFF) + amt, 255);
                    int g = (std::min)(static_cast<int>((c >> 8) & 0xFF) + amt, 255);
                    int r = (std::min)(static_cast<int>((c >> 16) & 0xFF) + amt, 255);
                    return static_cast<uint32_t>(b | (g << 8) | (r << 16));
                };
                uint32_t bgCrush = darken(bgNormal, 12);
                uint32_t bgClip = lighten(bgNormal, 16);
                bool isDark = Theme::IsDark();

                for (int row = 0; row < barAreaH; row++)
                {
                    for (int px = 0; px < barAreaW; px++)
                    {
                        uint32_t bg = bgNormal;
                        if (px < crushPx)
                            bg = bgCrush;
                        else if (px >= clipPx)
                            bg = bgClip;
                        pixels[row * barAreaW + px] = bg;
                    }
                }

                float sceneRange = m_histogram.log2Max - m_histogram.log2Min;

                auto getBinValue = [&](const std::array<float, HistogramData::kBinCount>& bins, int px) -> float
                {
                    float outLog2 = kDispMin + (static_cast<float>(px) + 0.5f) / barAreaW * kDispRange;
                    float sceneLog2 = outLog2 - m_exposure;
                    if (sceneRange <= 0.0f)
                        return 0.0f;
                    float t = (sceneLog2 - m_histogram.log2Min) / sceneRange;
                    int bin = static_cast<int>(t * HistogramData::kBinCount);
                    if (bin < 0 || bin >= HistogramData::kBinCount)
                        return 0.0f;
                    return bins[bin];
                };

                if (m_channelMode == 4 && m_histogram.isValid)
                {
                    for (int px = 0; px < barAreaW; px++)
                    {
                        int rBarH = static_cast<int>(getBinValue(m_histogram.red, px) * barAreaH);
                        int gBarH = static_cast<int>(getBinValue(m_histogram.green, px) * barAreaH);
                        int bBarH = static_cast<int>(getBinValue(m_histogram.blue, px) * barAreaH);

                        int maxH = rBarH;
                        if (gBarH > maxH)
                            maxH = gBarH;
                        if (bBarH > maxH)
                            maxH = bBarH;

                        for (int row = 0; row < maxH && row < barAreaH; row++)
                        {
                            uint32_t& p = pixels[row * barAreaW + px];
                            int pb = (p & 0xFF);
                            int pg = ((p >> 8) & 0xFF);
                            int pr = ((p >> 16) & 0xFF);
                            // Compute each channel's color independently from the background,
                            // then average the contributing channels to avoid order-dependent bias.
                            int count = (row < rBarH) + (row < gBarH) + (row < bBarH);
                            int sr = pr, sg = pg, sb = pb;
                            if (count > 0)
                            {
                                sr = 0;
                                sg = 0;
                                sb = 0;
                                if (row < rBarH)
                                {
                                    sr += pr + (220 - pr) * 40 / 100;
                                    sg += pg * 60 / 100;
                                    sb += pb * 60 / 100;
                                }
                                if (row < gBarH)
                                {
                                    sr += pr * 60 / 100;
                                    sg += pg + (200 - pg) * 40 / 100;
                                    sb += pb * 60 / 100;
                                }
                                if (row < bBarH)
                                {
                                    sr += pr * 60 / 100;
                                    sg += pg * 60 / 100;
                                    sb += pb + (220 - pb) * 40 / 100;
                                }
                                sr /= count;
                                sg /= count;
                                sb /= count;
                            }
                            if (sr > 255)
                                sr = 255;
                            if (sr < 0)
                                sr = 0;
                            if (sg > 255)
                                sg = 255;
                            if (sg < 0)
                                sg = 0;
                            if (sb > 255)
                                sb = 255;
                            if (sb < 0)
                                sb = 0;
                            p = static_cast<uint32_t>(sb | (sg << 8) | (sr << 16));
                        }
                    }

                    // Luminance curve
                    std::vector<float> lumHeights(barAreaW);
                    for (int px = 0; px < barAreaW; px++)
                        lumHeights[px] = getBinValue(m_histogram.luminance, px) * barAreaH;

                    // Blend toward a contrasting color for the luminance curve
                    auto blendCurve = [&](int px, int row, float alpha)
                    {
                        if (row < 0 || row >= barAreaH || px < 0 || px >= barAreaW)
                            return;
                        uint32_t& p = pixels[row * barAreaW + px];
                        int pb = (p & 0xFF);
                        int pg = ((p >> 8) & 0xFF);
                        int pr = ((p >> 16) & 0xFF);
                        // Blend toward white on dark, toward dark gray on light
                        int target = isDark ? 255 : 0;
                        int a = static_cast<int>(alpha * 255.0f);
                        pr = pr + (target - pr) * a / 255;
                        pg = pg + (target - pg) * a / 255;
                        pb = pb + (target - pb) * a / 255;
                        p = static_cast<uint32_t>(pb | (pg << 8) | (pr << 16));
                    };

                    for (int px = 0; px < barAreaW; px++)
                    {
                        float h0 = (px > 0) ? lumHeights[px - 1] : lumHeights[px];
                        float h1 = lumHeights[px];

                        float lo = (std::min)(h0, h1);
                        float hi = (std::max)(h0, h1);
                        int rowLo = static_cast<int>(lo);
                        int rowHi = static_cast<int>(hi);

                        int coreRow = static_cast<int>(h1);
                        if (coreRow >= barAreaH)
                            coreRow = barAreaH - 1;

                        for (int row = rowLo; row <= rowHi && row < barAreaH; row++)
                            blendCurve(px, row, 0.1f);

                        if (coreRow >= 0)
                            blendCurve(px, coreRow, 0.25f);
                        if (coreRow >= 1)
                            blendCurve(px, coreRow - 1, 0.1f);
                        if (coreRow + 1 < barAreaH)
                            blendCurve(px, coreRow + 1, 0.1f);
                    }
                }
                else if (m_histogram.isValid)
                {
                    const auto* bins = &m_histogram.luminance;
                    COLORREF color = ChannelColor(m_channelMode);
                    if (m_channelMode == 1)
                        bins = &m_histogram.red;
                    else if (m_channelMode == 2)
                        bins = &m_histogram.green;
                    else if (m_channelMode == 3)
                        bins = &m_histogram.blue;

                    int cr = GetRValue(color), cg = GetGValue(color), cb = GetBValue(color);

                    for (int px = 0; px < barAreaW; px++)
                    {
                        float val = getBinValue(*bins, px);
                        int barH = static_cast<int>(val * barAreaH);
                        if (barH < 1 && val > 0.0f)
                            barH = 1;
                        for (int row = 0; row < barH && row < barAreaH; row++)
                            pixels[row * barAreaW + px] = static_cast<uint32_t>(cb | (cg << 8) | (cr << 16));
                    }
                }

                auto drawLine = [&](int px, uint32_t color)
                {
                    if (px >= 0 && px < barAreaW)
                    {
                        for (int row = 0; row < barAreaH; row++)
                            pixels[row * barAreaW + px] = color;
                    }
                };

                uint32_t lineColor = isDark ? 0x00505050 : 0x00AAAAAA;
                drawLine(crushPx, lineColor);
                drawLine(clipPx, lineColor);

                HDC histDC = CreateCompatibleDC(memDC);
                HBITMAP oldHistBmp = static_cast<HBITMAP>(SelectObject(histDC, histBmp));
                BitBlt(memDC, histRect.left, histRect.top, barAreaW, barAreaH, histDC, 0, 0, SRCCOPY);
                SelectObject(histDC, oldHistBmp);
                DeleteDC(histDC);
            }
            if (histBmp)
                DeleteObject(histBmp);
        }
    }

    y = histRect.bottom + MulDiv(4, dpi, 96);
    // Skip channel buttons area
    y += MulDiv(22, dpi, 96) + m;

    // --- Compare section ---
    // Section header (always visible)
    {
        RECT compareLabelRect = {m, y, w - m, y + labelH};
        DrawTextW(memDC, L"Compare", -1, &compareLabelRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        y += labelH + MulDiv(2, dpi, 96);
    }

    // Skip mode buttons (always visible)
    y += MulDiv(22, dpi, 96) + m;

    // Detail section (only when compare active)
    if (m_compareMode > 0)
    {
        bool isSplit = (m_compareMode == 1);
        const wchar_t* basePrefix = isSplit ? L"L: " : L"Base: ";
        const wchar_t* blendPrefix = isSplit ? L"R: " : L"Blend: ";

        // Base/Left label (highlighted if focused)
        {
            COLORREF baseColor = (m_compareFocus == 0) ? Colors::Selection : labelColor;
            SetTextColor(memDC, baseColor);
            std::wstring text = basePrefix + m_compareLabelBase;
            RECT r = {m, y, w - m, y + labelH};
            DrawTextW(memDC, text.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            y += labelH;
        }

        // Blend/Right label (highlighted if focused)
        {
            COLORREF blendColor = (m_compareFocus == 1) ? Colors::Selection : labelColor;
            SetTextColor(memDC, blendColor);
            std::wstring text = blendPrefix + m_compareLabelBlend;
            RECT r = {m, y, w - m, y + labelH};
            DrawTextW(memDC, text.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            y += labelH + MulDiv(4, dpi, 96);
        }
        SetTextColor(memDC, labelColor);

        // Skip focus + swap button row
        y += MulDiv(22, dpi, 96) + m;
    }

    // --- Exposure section ---
    wchar_t expLabel[64];
    swprintf_s(expLabel, L"Exposure: %+.2f EV", m_exposure);
    RECT expLabelRect = {m, y, w - m, y + labelH};
    DrawTextW(memDC, expLabel, -1, &expLabelRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    y += labelH + MulDiv(2, dpi, 96);
    y += MulDiv(24, dpi, 96) + m; // trackbar height

    // --- Gamma section (SDR only) ---
    if (!m_isHDR)
    {
        float displayGamma = (m_gamma > 0.0f) ? 1.0f / m_gamma : 0.0f;
        wchar_t gammaLabel[64];
        swprintf_s(gammaLabel, L"Gamma: %.1f", displayGamma);
        RECT gammaLabelRect = {m, y, w - m, y + labelH};
        DrawTextW(memDC, gammaLabel, -1, &gammaLabelRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        y += labelH + MulDiv(2, dpi, 96);
        y += MulDiv(24, dpi, 96) + m; // trackbar height
    }

    // --- Layer browser section ---
    if (m_layerInfo.layers.size() > 1)
    {
        RECT layerLabelRect = {m, y, w - m, y + labelH};
        DrawTextW(memDC, L"Layers", -1, &layerLabelRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }

    // Blit
    BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);

    EndPaint(m_hwnd, &ps);
}

LRESULT CALLBACK Sidebar::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Sidebar* self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Sidebar*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<Sidebar*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_PAINT:
        self->OnPaint();
        return 0;

    case WM_SIZE:
        self->LayoutControls();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_HSCROLL:
    {
        if (self->m_suppressTrackbar)
            return 0;

        HWND trackHwnd = reinterpret_cast<HWND>(lParam);
        if (trackHwnd == self->m_exposureTrack)
        {
            int pos = static_cast<int>(SendMessageW(self->m_exposureTrack, TBM_GETPOS, 0, 0));
            float ev = ExposureFromTrackPos(pos);
            self->m_exposure = ev;
            InvalidateRect(hwnd, nullptr, FALSE);
            if (self->onExposureChange)
                self->onExposureChange(ev);
        }
        else if (trackHwnd == self->m_gammaTrack)
        {
            int pos = static_cast<int>(SendMessageW(self->m_gammaTrack, TBM_GETPOS, 0, 0));
            float g = GammaFromTrackPos(pos);
            self->m_gamma = g;
            InvalidateRect(hwnd, nullptr, FALSE);
            if (self->onGammaChange)
                self->onGammaChange(g);
        }
        return 0;
    }

    case WM_NOTIFY:
        return 0;

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        // Owner-drawn buttons send BN_DOUBLECLICKED on rapid clicks;
        // treat it the same as BN_CLICKED everywhere.
        if (code == BN_DOUBLECLICKED)
            code = BN_CLICKED;

        if (id == kAutoExpButtonId && code == BN_CLICKED)
        {
            if (self->onAutoExposure)
                self->onAutoExposure();
        }
        else if (id >= kChannelBtnBaseId && id < kChannelBtnBaseId + 5 && code == BN_CLICKED)
        {
            int channel = id - kChannelBtnBaseId;
            if (channel != self->m_channelMode)
            {
                self->m_channelMode = channel;
                for (auto btn : self->m_channelButtons)
                    InvalidateRect(btn, nullptr, FALSE);
                if (self->onHistogramChannel)
                    self->onHistogramChannel(channel);
            }
        }
        else if (id >= kCompareModeBtnBaseId && id < kCompareModeBtnBaseId + kNumCompareModes && code == BN_CLICKED)
        {
            int mode = id - kCompareModeBtnBaseId;
            if (self->onCompareMode)
                self->onCompareMode(mode);
        }
        else if (id >= kCompareFocusBtnBaseId && id < kCompareFocusBtnBaseId + 2 && code == BN_CLICKED)
        {
            int source = id - kCompareFocusBtnBaseId; // 0=A, 1=B
            if (source == 0 && self->onCompareFocusA)
                self->onCompareFocusA();
            else if (source == 1 && self->onCompareFocusB)
                self->onCompareFocusB();
        }
        else if (id == kCompareActionBtnBaseId && code == BN_CLICKED)
        {
            if (self->onCompareSwap)
                self->onCompareSwap();
        }
        else if (id == kLayerListId && code == LBN_SELCHANGE)
        {
            if (!self->m_suppressLayerChange)
            {
                int listIdx = static_cast<int>(SendMessageW(self->m_layerList, LB_GETCURSEL, 0, 0));
                if (listIdx < 0)
                    break;

                // Check if this is a header item (non-selectable)
                LRESULT data = SendMessageW(self->m_layerList, LB_GETITEMDATA, listIdx, 0);
                if (data & 0x80000000)
                {
                    // Revert selection to active layer
                    if (self->m_activeLayer >= 0 &&
                        self->m_activeLayer < static_cast<int>(self->m_layerListMapping.size()))
                        SendMessageW(self->m_layerList, LB_SETCURSEL, self->m_layerListMapping[self->m_activeLayer], 0);
                    break;
                }

                // Map listbox index to ExrLayer index
                int layerIdx = -1;
                for (int i = 0; i < static_cast<int>(self->m_layerListMapping.size()); i++)
                {
                    if (self->m_layerListMapping[i] == listIdx)
                    {
                        layerIdx = i;
                        break;
                    }
                }

                if (layerIdx >= 0 && layerIdx != self->m_activeLayer && self->onLayerSelect)
                {
                    self->m_activeLayer = layerIdx;
                    self->onLayerSelect(layerIdx);
                }
            }
        }
        return 0;
    }

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);

        // Owner-drawn channel buttons
        if (dis->CtlID >= static_cast<UINT>(kChannelBtnBaseId) && dis->CtlID < static_cast<UINT>(kChannelBtnBaseId + 5))
        {
            int channel = static_cast<int>(dis->CtlID) - kChannelBtnBaseId;
            bool active = (channel == self->m_channelMode);
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            bool disabled = (dis->itemState & ODS_DISABLED) != 0;

            // Channel accent colors
            static const COLORREF kChannelAccent[] = {
                RGB(200, 200, 200), // Luminance — neutral
                RGB(220, 60, 60),   // Red
                RGB(60, 200, 60),   // Green
                RGB(60, 100, 220),  // Blue
                RGB(200, 200, 200), // All — neutral
            };

            COLORREF bgColor;
            if (pressed)
                bgColor = Colors::ButtonPressed;
            else if (active)
                bgColor = Colors::Surface;
            else
                bgColor = Colors::Background;
            HBRUSH bg = CreateSolidBrush(bgColor);
            FillRect(dis->hDC, &dis->rcItem, bg);
            DeleteObject(bg);

            // Segmented control border — shared edges, no double lines
            COLORREF borderColor = Colors::ButtonBorder;
            HPEN borderPen = CreatePen(PS_SOLID, 1, borderColor);
            HPEN oldPen = static_cast<HPEN>(SelectObject(dis->hDC, borderPen));
            int L = dis->rcItem.left, T = dis->rcItem.top;
            int R = dis->rcItem.right - 1, B = dis->rcItem.bottom - 1;
            // Top edge
            MoveToEx(dis->hDC, L, T, nullptr);
            LineTo(dis->hDC, R + 1, T);
            // Bottom edge
            MoveToEx(dis->hDC, L, B, nullptr);
            LineTo(dis->hDC, R + 1, B);
            // Left edge only on first button
            if (channel == 0)
            {
                MoveToEx(dis->hDC, L, T, nullptr);
                LineTo(dis->hDC, L, B + 1);
            }
            // Right edge (doubles as separator for next button)
            MoveToEx(dis->hDC, R, T, nullptr);
            LineTo(dis->hDC, R, B + 1);
            SelectObject(dis->hDC, oldPen);
            DeleteObject(borderPen);

            // Active indicator — thin colored bar at bottom
            if (active && !disabled)
            {
                RECT indicator = dis->rcItem;
                indicator.top = indicator.bottom - 2;
                indicator.left += 1; // inset within border
                indicator.right -= 1;
                HBRUSH accentBrush = CreateSolidBrush(kChannelAccent[channel]);
                FillRect(dis->hDC, &indicator, accentBrush);
                DeleteObject(accentBrush);
            }

            // Text
            SetBkMode(dis->hDC, TRANSPARENT);
            COLORREF textColor;
            if (disabled)
                textColor = Colors::TextDim;
            else if (active && channel >= 1 && channel <= 3)
                textColor = kChannelAccent[channel]; // colored text for active R/G/B
            else
                textColor = Colors::TextPrimary;
            SetTextColor(dis->hDC, textColor);

            HFONT font = self->m_font ? self->m_font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, font));
            // Get button text
            wchar_t label[8] = {};
            GetWindowTextW(dis->hwndItem, label, 8);
            DrawTextW(dis->hDC, label, -1, &dis->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            SelectObject(dis->hDC, oldFont);

            return TRUE;
        }

        // Owner-drawn compare mode buttons (segmented like channel buttons)
        if (dis->CtlID >= static_cast<UINT>(kCompareModeBtnBaseId) &&
            dis->CtlID < static_cast<UINT>(kCompareModeBtnBaseId + kNumCompareModes))
        {
            int idx = static_cast<int>(dis->CtlID) - kCompareModeBtnBaseId;
            bool active = (idx == self->m_compareMode);
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;

            COLORREF bgColor = pressed ? Colors::ButtonPressed : active ? Colors::Surface : Colors::Background;
            HBRUSH bg = CreateSolidBrush(bgColor);
            FillRect(dis->hDC, &dis->rcItem, bg);
            DeleteObject(bg);

            // Segmented border
            HPEN borderPen = CreatePen(PS_SOLID, 1, Colors::ButtonBorder);
            HPEN oldPen = static_cast<HPEN>(SelectObject(dis->hDC, borderPen));
            int L = dis->rcItem.left, T = dis->rcItem.top;
            int R = dis->rcItem.right - 1, B = dis->rcItem.bottom - 1;
            MoveToEx(dis->hDC, L, T, nullptr);
            LineTo(dis->hDC, R + 1, T);
            MoveToEx(dis->hDC, L, B, nullptr);
            LineTo(dis->hDC, R + 1, B);
            if (idx == 0)
            {
                MoveToEx(dis->hDC, L, T, nullptr);
                LineTo(dis->hDC, L, B + 1);
            }
            MoveToEx(dis->hDC, R, T, nullptr);
            LineTo(dis->hDC, R, B + 1);
            SelectObject(dis->hDC, oldPen);
            DeleteObject(borderPen);

            // Active indicator
            if (active)
            {
                RECT indicator = dis->rcItem;
                indicator.top = indicator.bottom - 2;
                indicator.left += 1;
                indicator.right -= 1;
                HBRUSH accentBrush = CreateSolidBrush(Colors::Selection);
                FillRect(dis->hDC, &indicator, accentBrush);
                DeleteObject(accentBrush);
            }

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, active ? Colors::TextBright : Colors::TextPrimary);
            HFONT font = self->m_font ? self->m_font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, font));
            wchar_t label[16] = {};
            GetWindowTextW(dis->hwndItem, label, 16);
            DrawTextW(dis->hDC, label, -1, &dis->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            SelectObject(dis->hDC, oldFont);
            return TRUE;
        }

        // Owner-drawn compare focus buttons (segmented pair with active indicator)
        if (dis->CtlID >= static_cast<UINT>(kCompareFocusBtnBaseId) &&
            dis->CtlID < static_cast<UINT>(kCompareFocusBtnBaseId + 2))
        {
            int idx = static_cast<int>(dis->CtlID) - kCompareFocusBtnBaseId;
            bool active = (idx == self->m_compareFocus);
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;

            COLORREF bgColor = pressed ? Colors::ButtonPressed : active ? Colors::Surface : Colors::Background;
            HBRUSH bg = CreateSolidBrush(bgColor);
            FillRect(dis->hDC, &dis->rcItem, bg);
            DeleteObject(bg);

            HPEN borderPen = CreatePen(PS_SOLID, 1, Colors::ButtonBorder);
            HPEN oldPen = static_cast<HPEN>(SelectObject(dis->hDC, borderPen));
            int L = dis->rcItem.left, T = dis->rcItem.top;
            int R = dis->rcItem.right - 1, B = dis->rcItem.bottom - 1;
            MoveToEx(dis->hDC, L, T, nullptr);
            LineTo(dis->hDC, R + 1, T);
            MoveToEx(dis->hDC, L, B, nullptr);
            LineTo(dis->hDC, R + 1, B);
            if (idx == 0)
            {
                MoveToEx(dis->hDC, L, T, nullptr);
                LineTo(dis->hDC, L, B + 1);
            }
            MoveToEx(dis->hDC, R, T, nullptr);
            LineTo(dis->hDC, R, B + 1);
            SelectObject(dis->hDC, oldPen);
            DeleteObject(borderPen);

            if (active)
            {
                RECT indicator = dis->rcItem;
                indicator.top = indicator.bottom - 2;
                indicator.left += 1;
                indicator.right -= 1;
                HBRUSH accentBrush = CreateSolidBrush(Colors::Selection);
                FillRect(dis->hDC, &indicator, accentBrush);
                DeleteObject(accentBrush);
            }

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, active ? Colors::TextBright : Colors::TextPrimary);
            HFONT font = self->m_font ? self->m_font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, font));
            wchar_t label[16] = {};
            GetWindowTextW(dis->hwndItem, label, 16);
            DrawTextW(dis->hDC, label, -1, &dis->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            SelectObject(dis->hDC, oldFont);
            return TRUE;
        }

        // Owner-drawn compare swap button (same style as Auto)
        if (dis->CtlID == static_cast<UINT>(kCompareActionBtnBaseId))
        {
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            COLORREF bgColor = pressed ? Colors::ButtonPressed : Colors::Background;
            HBRUSH bg = CreateSolidBrush(bgColor);
            FillRect(dis->hDC, &dis->rcItem, bg);
            DeleteObject(bg);

            HPEN borderPen = CreatePen(PS_SOLID, 1, Colors::ButtonBorder);
            HPEN oldPen = static_cast<HPEN>(SelectObject(dis->hDC, borderPen));
            HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(dis->hDC, GetStockObject(NULL_BRUSH)));
            Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
            SelectObject(dis->hDC, oldPen);
            SelectObject(dis->hDC, oldBr);
            DeleteObject(borderPen);

            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, Colors::TextPrimary);
            HFONT font = self->m_font ? self->m_font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, font));
            wchar_t label[16] = {};
            GetWindowTextW(dis->hwndItem, label, 16);
            DrawTextW(dis->hDC, label, -1, &dis->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            SelectObject(dis->hDC, oldFont);
            return TRUE;
        }

        // Owner-drawn Auto button
        if (dis->CtlID == static_cast<UINT>(kAutoExpButtonId))
        {
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            bool disabled = (dis->itemState & ODS_DISABLED) != 0;

            COLORREF bgColor = pressed ? Colors::ButtonPressed : Colors::Background;
            HBRUSH bg = CreateSolidBrush(bgColor);
            FillRect(dis->hDC, &dis->rcItem, bg);
            DeleteObject(bg);

            // Border
            HPEN borderPen = CreatePen(PS_SOLID, 1, Colors::ButtonBorder);
            HPEN oldPen = static_cast<HPEN>(SelectObject(dis->hDC, borderPen));
            HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(dis->hDC, GetStockObject(NULL_BRUSH)));
            Rectangle(dis->hDC, dis->rcItem.left, dis->rcItem.top, dis->rcItem.right, dis->rcItem.bottom);
            SelectObject(dis->hDC, oldPen);
            SelectObject(dis->hDC, oldBr);
            DeleteObject(borderPen);

            // Text
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, disabled ? Colors::TextDim : Colors::TextPrimary);
            HFONT font = self->m_font ? self->m_font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
            HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, font));
            DrawTextW(dis->hDC, L"Auto", -1, &dis->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            SelectObject(dis->hDC, oldFont);

            if (dis->itemState & ODS_FOCUS)
                DrawFocusRect(dis->hDC, &dis->rcItem);

            return TRUE;
        }

        // Owner-drawn layer listbox
        if (dis->CtlID == static_cast<UINT>(kLayerListId) && dis->itemID != static_cast<UINT>(-1))
        {
            int dpi = GetDpiForWindow(hwnd);
            int indentPx = MulDiv(14, dpi, 96);

            LRESULT itemData = SendMessageW(dis->hwndItem, LB_GETITEMDATA, dis->itemID, 0);
            int indent = static_cast<int>(itemData & 0x7FFFFFFF);
            bool isHeader = (itemData & 0x80000000) != 0;

            // Get item text
            wchar_t text[256] = {};
            SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, reinterpret_cast<LPARAM>(text));

            // Background
            bool selected = (dis->itemState & ODS_SELECTED) != 0;
            COLORREF bgColor = selected ? Colors::Selection : Colors::Background;
            if (isHeader)
                bgColor = Colors::Background;
            HBRUSH bg = CreateSolidBrush(bgColor);
            FillRect(dis->hDC, &dis->rcItem, bg);
            DeleteObject(bg);

            SetBkMode(dis->hDC, TRANSPARENT);

            COLORREF textColor;
            if (isHeader)
                textColor = Colors::TextDim;
            else if (selected)
                textColor = Colors::SelectionText;
            else
                textColor = Colors::TextPrimary;
            SetTextColor(dis->hDC, textColor);

            // Draw tree connectors for indented items
            int xOffset = dis->rcItem.left + MulDiv(4, dpi, 96) + indent * indentPx;

            if (indent > 0 && !isHeader)
            {
                int connX = dis->rcItem.left + MulDiv(4, dpi, 96) + (indent - 1) * indentPx + indentPx / 2;
                int midY = (dis->rcItem.top + dis->rcItem.bottom) / 2;

                HPEN pen = CreatePen(PS_SOLID, 1, Colors::Connector);
                HPEN oldPen = static_cast<HPEN>(SelectObject(dis->hDC, pen));

                bool isLast = true;
                int nextIdx = static_cast<int>(dis->itemID) + 1;
                int count = static_cast<int>(SendMessageW(dis->hwndItem, LB_GETCOUNT, 0, 0));
                if (nextIdx < count)
                {
                    LRESULT nextData = SendMessageW(dis->hwndItem, LB_GETITEMDATA, nextIdx, 0);
                    int nextIndent = static_cast<int>(nextData & 0x7FFFFFFF);
                    if (nextIndent >= indent)
                        isLast = false;
                }

                MoveToEx(dis->hDC, connX, dis->rcItem.top, nullptr);
                LineTo(dis->hDC, connX, isLast ? midY : dis->rcItem.bottom);
                MoveToEx(dis->hDC, connX, midY, nullptr);
                LineTo(dis->hDC, connX + MulDiv(6, dpi, 96), midY);

                SelectObject(dis->hDC, oldPen);
                DeleteObject(pen);
            }

            // Draw text
            RECT textRect = dis->rcItem;
            textRect.left = xOffset;
            HFONT font =
                self->m_font ? self->m_font : reinterpret_cast<HFONT>(SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0));
            HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, font));
            DrawTextW(dis->hDC, text, -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(dis->hDC, oldFont);

            if (dis->itemState & ODS_FOCUS)
                DrawFocusRect(dis->hDC, &dis->rcItem);
        }
        return TRUE;
    }

    case WM_MEASUREITEM:
    {
        auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (mis->CtlID == static_cast<UINT>(kLayerListId))
        {
            int dpi = GetDpiForWindow(hwnd);
            mis->itemHeight = MulDiv(18, dpi, 96);
        }
        return TRUE;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSCROLLBAR:
    {
        HDC childDC = reinterpret_cast<HDC>(wParam);
        SetBkColor(childDC, Colors::Background);
        SetTextColor(childDC, Colors::TextPrimary);
        return reinterpret_cast<LRESULT>(Theme::GetBackgroundBrush());
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
