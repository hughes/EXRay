// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "sidebar.h"

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

    m_hwnd = CreateWindowExW(0, kSidebarClassName, nullptr, WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent, nullptr,
                             hInstance, this);
    if (!m_hwnd)
        return false;

    // Get font from parent's status bar for consistency
    HWND statusBar = FindWindowExW(parent, nullptr, STATUSCLASSNAMEW, nullptr);
    HFONT font = nullptr;
    if (statusBar)
        font = reinterpret_cast<HFONT>(SendMessageW(statusBar, WM_GETFONT, 0, 0));
    if (!font)
        font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    // Channel selector combo box
    m_channelCombo =
        CreateWindowExW(0, WC_COMBOBOXW, nullptr, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP, 0, 0, 0, 0,
                        m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChannelComboId)), hInstance, nullptr);
    SendMessageW(m_channelCombo, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    SendMessageW(m_channelCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Luminance"));
    SendMessageW(m_channelCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Red"));
    SendMessageW(m_channelCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Green"));
    SendMessageW(m_channelCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Blue"));
    SendMessageW(m_channelCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All Channels"));
    SendMessageW(m_channelCombo, CB_SETCURSEL, 4, 0); // Default: All

    // Exposure trackbar
    m_exposureTrack =
        CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 0, 0, 0, 0, m_hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExposureTrackId)), hInstance, nullptr);
    SendMessageW(m_exposureTrack, TBM_SETRANGEMIN, FALSE, kExpTrackMin);
    SendMessageW(m_exposureTrack, TBM_SETRANGEMAX, FALSE, kExpTrackMax);
    SendMessageW(m_exposureTrack, TBM_SETPOS, TRUE, 0);
    SendMessageW(m_exposureTrack, TBM_SETLINESIZE, 0, 1); // arrow keys = 0.25 EV
    SendMessageW(m_exposureTrack, TBM_SETPAGESIZE, 0, 4); // page = 1.0 EV

    // Auto-exposure button
    m_autoExpButton =
        CreateWindowExW(0, WC_BUTTONW, L"Auto", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, m_hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutoExpButtonId)), hInstance, nullptr);
    SendMessageW(m_autoExpButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);

    // Gamma trackbar
    m_gammaTrack =
        CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, 0, 0, 0, 0, m_hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGammaTrackId)), hInstance, nullptr);
    SendMessageW(m_gammaTrack, TBM_SETRANGEMIN, FALSE, kGammaTrackMin);
    SendMessageW(m_gammaTrack, TBM_SETRANGEMAX, FALSE, kGammaTrackMax);
    SendMessageW(m_gammaTrack, TBM_SETPOS, TRUE, TrackPosFromGamma(m_gamma));
    SendMessageW(m_gammaTrack, TBM_SETLINESIZE, 0, 1); // arrow keys = 0.05
    SendMessageW(m_gammaTrack, TBM_SETPAGESIZE, 0, 2); // page = 0.10

    // Layer listbox (hidden until layers are set) — owner-draw for hierarchical indentation
    m_layerList =
        CreateWindowExW(0, WC_LISTBOXW, nullptr,
                        WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                        0, 0, 0, 0, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kLayerListId)), hInstance, nullptr);
    SendMessageW(m_layerList, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);

    return true;
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
    EnableWindow(m_exposureTrack, enabled);
    EnableWindow(m_gammaTrack, enabled);
    EnableWindow(m_autoExpButton, enabled);
    EnableWindow(m_channelCombo, enabled);
    EnableWindow(m_layerList, enabled);
}

void Sidebar::SetHistogramData(const HistogramData& data, int channelMode)
{
    m_histogram = data;
    m_channelMode = channelMode;
    SendMessageW(m_channelCombo, CB_SETCURSEL, channelMode, 0);
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
                // Mip child — indented under parent layer
                indent = multiPart ? 2 : 1;
                wchar_t buf[64];
                swprintf_s(buf, L"Mip %d  %d\u00D7%d", layer.mipLevel, layer.mipWidth, layer.mipHeight);
                label = buf;
            }
            else
            {
                // Top-level layer (or mip level 0)
                if (multiPart)
                {
                    indent = 1; // layers indent under part
                    // Check if this is a new part (show part header via indent 0)
                    bool newPart = (idx == 0) || (info.layers[idx - 1].partIndex != layer.partIndex);
                    if (newPart)
                    {
                        // Insert a non-selectable part header
                        std::wstring partLabel;
                        if (!layer.partName.empty())
                            partLabel = toWide(layer.partName);
                        else
                        {
                            wchar_t buf[16];
                            swprintf_s(buf, L"Part %d", layer.partIndex);
                            partLabel = buf;
                        }
                        int partIdx = static_cast<int>(SendMessageW(m_layerList, LB_ADDSTRING, 0,
                                                                     reinterpret_cast<LPARAM>(partLabel.c_str())));
                        // indent=0, flags: high bit marks as header (non-selectable)
                        SendMessageW(m_layerList, LB_SETITEMDATA, partIdx, 0x80000000);
                    }
                }

                // Layer name
                if (layer.name.empty())
                    label = L"(default)";
                else
                    label = toWide(layer.name);

                // Append channel list
                label += L"  ";
                for (size_t i = 0; i < layer.channels.size(); i++)
                {
                    if (i > 0)
                        label += L",";
                    label += toWide(layer.channels[i]);
                }

                // Show dimensions for mip level 0 if mipmaps exist
                if (layer.numMipLevels > 1)
                {
                    wchar_t buf[32];
                    swprintf_s(buf, L"  %d\u00D7%d", layer.mipWidth, layer.mipHeight);
                    label += buf;
                }
            }

            int listIdx = static_cast<int>(SendMessageW(m_layerList, LB_ADDSTRING, 0,
                                                         reinterpret_cast<LPARAM>(label.c_str())));
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
    int comboH = MulDiv(200, dpi, 96); // drop-down height
    int comboVisH = MulDiv(22, dpi, 96);
    int histH = MulDiv(kHistogramHeight, dpi, 96);

    int y = m;

    // Histogram area is painted in OnPaint — just skip past it
    // Label "Histogram" is drawn in OnPaint
    y += labelH + MulDiv(2, dpi, 96);
    // histogram rect
    y += histH + MulDiv(4, dpi, 96);

    // Channel combo
    MoveWindow(m_channelCombo, m, y, w - 2 * m, comboH, TRUE);
    y += comboVisH + m;

    // Exposure label is drawn in OnPaint
    y += labelH + MulDiv(2, dpi, 96);

    // Exposure trackbar + auto button
    int trackW = w - 2 * m - buttonW - MulDiv(4, dpi, 96);
    MoveWindow(m_exposureTrack, m, y, trackW, trackH, TRUE);
    MoveWindow(m_autoExpButton, m + trackW + MulDiv(4, dpi, 96), y + (trackH - buttonH) / 2, buttonW, buttonH, TRUE);
    y += trackH + m;

    // Gamma section — only in SDR mode
    if (!m_isHDR)
    {
        // Gamma label drawn in OnPaint
        y += labelH + MulDiv(2, dpi, 96);

        // Gamma trackbar
        MoveWindow(m_gammaTrack, m, y, w - 2 * m, trackH, TRUE);
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
            listH = MulDiv(36, dpi, 96); // minimum 2 rows
        MoveWindow(m_layerList, m, y, w - 2 * m, listH, TRUE);
    }
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
    HBRUSH bgBrush = CreateSolidBrush(RGB(0x2D, 0x2D, 0x2D));
    FillRect(memDC, &rc, bgBrush);
    DeleteObject(bgBrush);

    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(0xCC, 0xCC, 0xCC));
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    SelectObject(memDC, font);

    int y = m;

    // --- Histogram section ---
    RECT labelRect = {m, y, w - m, y + labelH};
    DrawTextW(memDC, L"Histogram", -1, &labelRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    y += labelH + MulDiv(2, dpi, 96);

    // Histogram background
    RECT histRect = {m, y, w - m, y + histH};
    HBRUSH histBg = CreateSolidBrush(RGB(0x1A, 0x1A, 0x1A));
    FillRect(memDC, &histRect, histBg);
    DeleteObject(histBg);

    // Draw histogram in output space (post-exposure log2)
    // X-axis = log2(scene_value) + exposure = log2(output_value)
    // Fixed display range so clip/crush markers stay in place
    {
        int barAreaW = histRect.right - histRect.left;
        int barAreaH = histRect.bottom - histRect.top;

        if (barAreaW > 0 && barAreaH > 0)
        {
            // Fixed output-space display range: -10 to +4 (14 stops)
            constexpr float kDispMin = -10.0f;
            constexpr float kDispMax = 4.0f;
            constexpr float kDispRange = kDispMax - kDispMin;

            // Fixed marker positions in output space
            constexpr float kClipLog2 = 0.0f;     // log2(1.0) — SDR white
            constexpr float kCrushLog2 = -6.644f; // log2(0.01) — 1% black

            float clipT = (kClipLog2 - kDispMin) / kDispRange;
            float crushT = (kCrushLog2 - kDispMin) / kDispRange;

            // Use a DIB section for all rendering (additive blending + overlays)
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
                // Background: darken crush region, brighten clip region
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

                uint32_t bgNormal = 0x001A1A1A; // RGB(0x1A, 0x1A, 0x1A)
                uint32_t bgCrush = 0x000E0E0E;  // darker
                uint32_t bgClip = 0x002A2A2A;   // brighter

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

                // Map histogram bins to output-space pixel positions
                // Bin i covers scene log2 range [binMin, binMax]
                // In output space: [binMin + exposure, binMax + exposure]
                float sceneRange = m_histogram.log2Max - m_histogram.log2Min;

                // Helper: get bar height for a given output-space pixel position
                auto getBinValue = [&](const std::array<float, HistogramData::kBinCount>& bins, int px) -> float
                {
                    // Map pixel to output log2 value
                    float outLog2 = kDispMin + (static_cast<float>(px) + 0.5f) / barAreaW * kDispRange;
                    // Map to scene log2 value
                    float sceneLog2 = outLog2 - m_exposure;
                    // Map to bin index
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
                    // All channels: additive RGB
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
                            if (row < rBarH)
                            {
                                pr += 90;
                                pg += 15;
                                pb += 15;
                            }
                            if (row < gBarH)
                            {
                                pr += 15;
                                pg += 80;
                                pb += 15;
                            }
                            if (row < bBarH)
                            {
                                pr += 15;
                                pg += 15;
                                pb += 90;
                            }
                            if (pr > 255)
                                pr = 255;
                            if (pg > 255)
                                pg = 255;
                            if (pb > 255)
                                pb = 255;
                            p = static_cast<uint32_t>(pb | (pg << 8) | (pr << 16));
                        }
                    }

                    // Luminance curve: smooth connected line with soft glow
                    // First pass: compute float heights for all pixels
                    std::vector<float> lumHeights(barAreaW);
                    for (int px = 0; px < barAreaW; px++)
                        lumHeights[px] = getBinValue(m_histogram.luminance, px) * barAreaH;

                    // Blend helper: additively blend white at given alpha onto a pixel
                    auto blendWhite = [&](int px, int row, float alpha)
                    {
                        if (row < 0 || row >= barAreaH || px < 0 || px >= barAreaW)
                            return;
                        uint32_t& p = pixels[row * barAreaW + px];
                        int pb = (p & 0xFF);
                        int pg = ((p >> 8) & 0xFF);
                        int pr = ((p >> 16) & 0xFF);
                        int add = static_cast<int>(alpha * 255.0f);
                        pr = (std::min)(pr + add, 255);
                        pg = (std::min)(pg + add, 255);
                        pb = (std::min)(pb + add, 255);
                        p = static_cast<uint32_t>(pb | (pg << 8) | (pr << 16));
                    };

                    // Draw: for each pixel, draw a line segment from prev height to current
                    for (int px = 0; px < barAreaW; px++)
                    {
                        float h0 = (px > 0) ? lumHeights[px - 1] : lumHeights[px];
                        float h1 = lumHeights[px];

                        // Interpolate to draw smooth vertical span between h0 and h1
                        float lo = (std::min)(h0, h1);
                        float hi = (std::max)(h0, h1);
                        int rowLo = static_cast<int>(lo);
                        int rowHi = static_cast<int>(hi);

                        // Core line at the top of the luminance value
                        int coreRow = static_cast<int>(h1);
                        if (coreRow >= barAreaH)
                            coreRow = barAreaH - 1;

                        // Vertical fill between connected heights (the "line")
                        for (int row = rowLo; row <= rowHi && row < barAreaH; row++)
                            blendWhite(px, row, 0.1f);

                        // Core pixel (brightest)
                        if (coreRow >= 0)
                            blendWhite(px, coreRow, 0.25f);

                        // Soft glow: 1px above and below core, dimmer
                        if (coreRow >= 1)
                            blendWhite(px, coreRow - 1, 0.1f);
                        if (coreRow + 1 < barAreaH)
                            blendWhite(px, coreRow + 1, 0.1f);
                    }
                }
                else if (m_histogram.isValid)
                {
                    // Single channel
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
                        {
                            // BGRA pixel format
                            pixels[row * barAreaW + px] = static_cast<uint32_t>(cb | (cg << 8) | (cr << 16));
                        }
                    }
                }

                // Draw boundary lines directly into the DIB
                auto drawLine = [&](int px, uint32_t color)
                {
                    if (px >= 0 && px < barAreaW)
                    {
                        for (int row = 0; row < barAreaH; row++)
                            pixels[row * barAreaW + px] = color;
                    }
                };

                drawLine(crushPx, 0x00505050); // subtle gray for crush
                drawLine(clipPx, 0x00707070);  // lighter gray for clip

                // Blit onto main buffer
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
    // Skip channel combo area
    y += MulDiv(22, dpi, 96) + m;

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

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);

        if (id == kAutoExpButtonId && code == BN_CLICKED)
        {
            if (self->onAutoExposure)
                self->onAutoExposure();
        }
        else if (id == kChannelComboId && code == CBN_SELCHANGE)
        {
            int sel = static_cast<int>(SendMessageW(self->m_channelCombo, CB_GETCURSEL, 0, 0));
            if (sel >= 0 && self->onHistogramChannel)
                self->onHistogramChannel(sel);
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
                        SendMessageW(self->m_layerList, LB_SETCURSEL,
                                     self->m_layerListMapping[self->m_activeLayer], 0);
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

    case WM_DRAWITEM:
    {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
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
            COLORREF bgColor = selected ? RGB(0x3A, 0x5A, 0x8A) : RGB(0x2D, 0x2D, 0x2D);
            if (isHeader)
                bgColor = RGB(0x2D, 0x2D, 0x2D); // headers don't highlight
            HBRUSH bg = CreateSolidBrush(bgColor);
            FillRect(dis->hDC, &dis->rcItem, bg);
            DeleteObject(bg);

            SetBkMode(dis->hDC, TRANSPARENT);

            // Text color varies by item type
            COLORREF textColor;
            if (isHeader)
                textColor = RGB(0x99, 0x99, 0x99); // dim for part headers
            else if (selected)
                textColor = RGB(0xFF, 0xFF, 0xFF);
            else
                textColor = RGB(0xCC, 0xCC, 0xCC);
            SetTextColor(dis->hDC, textColor);

            // Draw tree connectors for indented items
            int xOffset = dis->rcItem.left + MulDiv(4, dpi, 96) + indent * indentPx;

            if (indent > 0 && !isHeader)
            {
                // Draw connector line: ├ or └
                int connX = dis->rcItem.left + MulDiv(4, dpi, 96) + (indent - 1) * indentPx + indentPx / 2;
                int midY = (dis->rcItem.top + dis->rcItem.bottom) / 2;

                HPEN pen = CreatePen(PS_SOLID, 1, RGB(0x66, 0x66, 0x66));
                HPEN oldPen = static_cast<HPEN>(SelectObject(dis->hDC, pen));

                // Check if this is the last child at this indent level
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

                // Vertical line
                MoveToEx(dis->hDC, connX, dis->rcItem.top, nullptr);
                LineTo(dis->hDC, connX, isLast ? midY : dis->rcItem.bottom);
                // Horizontal stub
                MoveToEx(dis->hDC, connX, midY, nullptr);
                LineTo(dis->hDC, connX + MulDiv(6, dpi, 96), midY);

                SelectObject(dis->hDC, oldPen);
                DeleteObject(pen);
            }

            // Draw text
            RECT textRect = dis->rcItem;
            textRect.left = xOffset;
            HFONT font = reinterpret_cast<HFONT>(SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0));
            HFONT oldFont = static_cast<HFONT>(SelectObject(dis->hDC, font));
            DrawTextW(dis->hDC, text, -1, &textRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(dis->hDC, oldFont);

            // Focus rect
            if (dis->itemState & ODS_FOCUS)
                DrawFocusRect(dis->hDC, &dis->rcItem);
        }
        return TRUE;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    {
        // Dark theme for trackbar and listbox backgrounds
        HDC childDC = reinterpret_cast<HDC>(wParam);
        SetBkColor(childDC, RGB(0x2D, 0x2D, 0x2D));
        SetTextColor(childDC, RGB(0xCC, 0xCC, 0xCC));
        static HBRUSH s_darkBrush = CreateSolidBrush(RGB(0x2D, 0x2D, 0x2D));
        return reinterpret_cast<LRESULT>(s_darkBrush);
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
