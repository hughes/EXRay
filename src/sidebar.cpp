// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "sidebar.h"

#include <commctrl.h>
#include <algorithm>
#include <cmath>
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
    case 0: return RGB(200, 200, 200); // Luminance
    case 1: return RGB(220, 60, 60);   // Red
    case 2: return RGB(60, 200, 60);   // Green
    case 3: return RGB(60, 100, 220);  // Blue
    default: return RGB(200, 200, 200);
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
    m_channelCombo = CreateWindowExW(0, WC_COMBOBOXW, nullptr,
                                     WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
                                     0, 0, 0, 0, m_hwnd,
                                     reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChannelComboId)),
                                     hInstance, nullptr);
    SendMessageW(m_channelCombo, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
    SendMessageW(m_channelCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Luminance"));
    SendMessageW(m_channelCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Red"));
    SendMessageW(m_channelCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Green"));
    SendMessageW(m_channelCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Blue"));
    SendMessageW(m_channelCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All Channels"));
    SendMessageW(m_channelCombo, CB_SETCURSEL, 4, 0); // Default: All

    // Exposure trackbar
    m_exposureTrack = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
                                      WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                                      0, 0, 0, 0, m_hwnd,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExposureTrackId)),
                                      hInstance, nullptr);
    SendMessageW(m_exposureTrack, TBM_SETRANGEMIN, FALSE, kExpTrackMin);
    SendMessageW(m_exposureTrack, TBM_SETRANGEMAX, FALSE, kExpTrackMax);
    SendMessageW(m_exposureTrack, TBM_SETPOS, TRUE, 0);
    SendMessageW(m_exposureTrack, TBM_SETLINESIZE, 0, 1);  // arrow keys = 0.25 EV
    SendMessageW(m_exposureTrack, TBM_SETPAGESIZE, 0, 4);  // page = 1.0 EV

    // Auto-exposure button
    m_autoExpButton = CreateWindowExW(0, WC_BUTTONW, L"Auto",
                                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      0, 0, 0, 0, m_hwnd,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutoExpButtonId)),
                                      hInstance, nullptr);
    SendMessageW(m_autoExpButton, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);

    // Gamma trackbar
    m_gammaTrack = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
                                   WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                                   0, 0, 0, 0, m_hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kGammaTrackId)),
                                   hInstance, nullptr);
    SendMessageW(m_gammaTrack, TBM_SETRANGEMIN, FALSE, kGammaTrackMin);
    SendMessageW(m_gammaTrack, TBM_SETRANGEMAX, FALSE, kGammaTrackMax);
    SendMessageW(m_gammaTrack, TBM_SETPOS, TRUE, TrackPosFromGamma(m_gamma));
    SendMessageW(m_gammaTrack, TBM_SETLINESIZE, 0, 1);  // arrow keys = 0.05
    SendMessageW(m_gammaTrack, TBM_SETPAGESIZE, 0, 2);  // page = 0.10

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

void Sidebar::SetHistogramData(const HistogramData& data, int channelMode)
{
    m_histogram = data;
    m_channelMode = channelMode;
    SendMessageW(m_channelCombo, CB_SETCURSEL, channelMode, 0);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Sidebar::SetExposureGamma(float exposure, float gamma, bool isHDR)
{
    m_exposure = exposure;
    m_gamma = gamma;
    m_isHDR = isHDR;

    m_suppressTrackbar = true;
    SendMessageW(m_exposureTrack, TBM_SETPOS, TRUE, TrackPosFromExposure(exposure));
    SendMessageW(m_gammaTrack, TBM_SETPOS, TRUE, TrackPosFromGamma(gamma));
    m_suppressTrackbar = false;

    ShowWindow(m_gammaTrack, isHDR ? SW_HIDE : SW_SHOW);
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
            constexpr float kClipLog2 = 0.0f;       // log2(1.0) — SDR white
            constexpr float kCrushLog2 = -6.644f;    // log2(0.01) — 1% black

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
            HBITMAP histBmp = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS,
                                               reinterpret_cast<void**>(&pixels), nullptr, 0);
            if (histBmp && pixels)
            {
                // Background: darken crush region, brighten clip region
                int clipPx = static_cast<int>(clipT * barAreaW);
                int crushPx = static_cast<int>(crushT * barAreaW);
                if (clipPx < 0) clipPx = 0;
                if (clipPx > barAreaW) clipPx = barAreaW;
                if (crushPx < 0) crushPx = 0;
                if (crushPx > barAreaW) crushPx = barAreaW;

                uint32_t bgNormal = 0x001A1A1A;  // RGB(0x1A, 0x1A, 0x1A)
                uint32_t bgCrush  = 0x000E0E0E;  // darker
                uint32_t bgClip   = 0x002A2A2A;  // brighter

                for (int row = 0; row < barAreaH; row++)
                {
                    for (int px = 0; px < barAreaW; px++)
                    {
                        uint32_t bg = bgNormal;
                        if (px < crushPx) bg = bgCrush;
                        else if (px >= clipPx) bg = bgClip;
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
                    if (sceneRange <= 0.0f) return 0.0f;
                    float t = (sceneLog2 - m_histogram.log2Min) / sceneRange;
                    int bin = static_cast<int>(t * HistogramData::kBinCount);
                    if (bin < 0 || bin >= HistogramData::kBinCount) return 0.0f;
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
                        if (gBarH > maxH) maxH = gBarH;
                        if (bBarH > maxH) maxH = bBarH;

                        for (int row = 0; row < maxH && row < barAreaH; row++)
                        {
                            uint32_t& p = pixels[row * barAreaW + px];
                            int pb = (p & 0xFF);
                            int pg = ((p >> 8) & 0xFF);
                            int pr = ((p >> 16) & 0xFF);
                            if (row < rBarH) { pr += 90; pg += 15; pb += 15; }
                            if (row < gBarH) { pr += 15; pg += 80; pb += 15; }
                            if (row < bBarH) { pr += 15; pg += 15; pb += 90; }
                            if (pr > 255) pr = 255;
                            if (pg > 255) pg = 255;
                            if (pb > 255) pb = 255;
                            p = static_cast<uint32_t>(pb | (pg << 8) | (pr << 16));
                        }
                    }
                }
                else if (m_histogram.isValid)
                {
                    // Single channel
                    const auto* bins = &m_histogram.luminance;
                    COLORREF color = ChannelColor(m_channelMode);
                    if (m_channelMode == 1) bins = &m_histogram.red;
                    else if (m_channelMode == 2) bins = &m_histogram.green;
                    else if (m_channelMode == 3) bins = &m_histogram.blue;

                    int cr = GetRValue(color), cg = GetGValue(color), cb = GetBValue(color);

                    for (int px = 0; px < barAreaW; px++)
                    {
                        float val = getBinValue(*bins, px);
                        int barH = static_cast<int>(val * barAreaH);
                        if (barH < 1 && val > 0.0f) barH = 1;
                        for (int row = 0; row < barH && row < barAreaH; row++)
                        {
                            // BGRA pixel format
                            pixels[row * barAreaW + px] = static_cast<uint32_t>(
                                cb | (cg << 8) | (cr << 16));
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

                drawLine(crushPx, 0x00505050);  // subtle gray for crush
                drawLine(clipPx,  0x00707070);  // lighter gray for clip

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
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLORSTATIC:
    {
        // Dark theme for trackbar backgrounds
        HDC childDC = reinterpret_cast<HDC>(wParam);
        SetBkColor(childDC, RGB(0x2D, 0x2D, 0x2D));
        static HBRUSH s_darkBrush = CreateSolidBrush(RGB(0x2D, 0x2D, 0x2D));
        return reinterpret_cast<LRESULT>(s_darkBrush);
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
