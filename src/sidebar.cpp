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
#include <timeapi.h>  // timeBeginPeriod — accurate playback timer cadence
#include <windowsx.h> // GET_X_LPARAM

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

// Subclass for single-line numeric edits (frame number, fps): digits only,
// Enter commits by moving focus to the parent (triggering EN_KILLFOCUS),
// Ctrl+Backspace deletes back to the start, and the Enter beep is suppressed.
static LRESULT CALLBACK NumericEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    if (msg == WM_KEYDOWN && wParam == VK_RETURN)
    {
        SetFocus(GetParent(hwnd));
        return 0;
    }
    if (msg == WM_CHAR && wParam == L'\r')
        return 0; // suppress beep
    if (msg == WM_CHAR && wParam == 0x7F) // Ctrl+Backspace: delete from caret back to start
    {
        DWORD start = 0, end = 0;
        SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        SendMessageW(hwnd, EM_SETSEL, 0, start);
        SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
        return 0;
    }
    if (msg == WM_CHAR && wParam >= L' ' && (wParam < L'0' || wParam > L'9'))
        return 0; // ignore non-digits (allow control chars like backspace)
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

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

    // Sequence navigation buttons (hidden until sequence is active)
    m_seqFirstBtn = CreateWindowExW(0, WC_BUTTONW, L"\u00AB", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, m_hwnd,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSeqFirstBtnId)), hInstance, nullptr);
    m_seqPrevBtn = CreateWindowExW(0, WC_BUTTONW, L"\u2039", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, m_hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSeqPrevBtnId)), hInstance, nullptr);
    m_seqNextBtn = CreateWindowExW(0, WC_BUTTONW, L"\u203A", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, m_hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSeqNextBtnId)), hInstance, nullptr);
    m_seqLastBtn = CreateWindowExW(0, WC_BUTTONW, L"\u00BB", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, m_hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSeqLastBtnId)), hInstance, nullptr);
    m_seqFrameEdit =
        CreateWindowExW(0, WC_EDITW, L"", WS_CHILD | WS_BORDER | ES_CENTER, 0, 0, 0, 0, m_hwnd,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSeqFrameEditId)), hInstance, nullptr);
    Theme::ApplyToControl(m_seqFrameEdit);
    // Subclass prev/next buttons for hold-to-repeat
    auto navBtnSubclass = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                             DWORD_PTR refData) -> LRESULT
    {
        Sidebar* self = reinterpret_cast<Sidebar*>(refData);
        if (msg == WM_LBUTTONDOWN)
        {
            self->m_seqRepeatDelta = (GetDlgCtrlID(hwnd) == kSeqPrevBtnId) ? -1 : 1;
            SetTimer(self->m_hwnd, kSeqRepeatTimerId, kSeqRepeatInitialMs, nullptr);
        }
        else if (msg == WM_LBUTTONUP || msg == WM_CAPTURECHANGED)
        {
            KillTimer(self->m_hwnd, kSeqRepeatTimerId);
            self->m_seqRepeatDelta = 0;
        }
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    };
    SetWindowSubclass(m_seqPrevBtn, navBtnSubclass, 0, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(m_seqNextBtn, navBtnSubclass, 0, reinterpret_cast<DWORD_PTR>(this));

    // Digits-only + Enter-commits behavior for the frame-number edit.
    SetWindowSubclass(m_seqFrameEdit, NumericEditProc, 0, 0);

    // --- Playback controls (rows below the advance buttons; hidden until active) ---
    m_seqPlayBtn = CreateWindowExW(0, WC_BUTTONW, L"Play", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, m_hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSeqPlayBtnId)), hInstance, nullptr);
    m_seqFpsEdit = CreateWindowExW(0, WC_EDITW, L"24", WS_CHILD | WS_BORDER | ES_CENTER, 0, 0, 0, 0, m_hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSeqFpsEditId)), hInstance, nullptr);
    Theme::ApplyToControl(m_seqFpsEdit);
    SetWindowSubclass(m_seqFpsEdit, NumericEditProc, 0, 0);
    m_seqLoopBtn = CreateWindowExW(0, WC_BUTTONW, L"Loop", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, m_hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSeqLoopBtnId)), hInstance, nullptr);
    // (Timeline is custom-drawn in OnPaint — no child control.)

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
    if (m_seqFrameEdit)
        SendMessageW(m_seqFrameEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (m_seqFpsEdit)
        SendMessageW(m_seqFpsEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
    EnableWindow(m_seqFirstBtn, enabled);
    EnableWindow(m_seqPrevBtn, enabled);
    EnableWindow(m_seqNextBtn, enabled);
    EnableWindow(m_seqLastBtn, enabled);
    EnableWindow(m_seqFrameEdit, enabled);
    EnableWindow(m_seqPlayBtn, enabled);
    EnableWindow(m_seqFpsEdit, enabled);
    EnableWindow(m_seqLoopBtn, enabled);
    if (!enabled)
        StopPlayback();
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

void Sidebar::SetSequenceState(int currentIndex, int currentFrame, int totalFrames)
{
    bool wasActive = m_seqActive;
    bool totalsChanged = (totalFrames != m_seqTotalFrames);
    m_seqActive = true;
    m_seqCurrentIndex = currentIndex;
    m_seqCurrentFrame = currentFrame;
    m_seqTotalFrames = totalFrames;

    if (!wasActive)
    {
        ShowWindow(m_seqFirstBtn, SW_SHOW);
        ShowWindow(m_seqPrevBtn, SW_SHOW);
        ShowWindow(m_seqNextBtn, SW_SHOW);
        ShowWindow(m_seqLastBtn, SW_SHOW);
        ShowWindow(m_seqFrameEdit, SW_SHOW);
        ShowWindow(m_seqPlayBtn, SW_SHOW);
        ShowWindow(m_seqFpsEdit, SW_SHOW);
        ShowWindow(m_seqLoopBtn, SW_SHOW);
        UpdatePlayButtonLabel();
        UpdateLoopButtonLabel();
        if (onLoopModeChanged)
            onLoopModeChanged(m_loopMode); // set the cache's prefetch policy
        m_cacheStates.clear();
        SetTimer(m_hwnd, kSeqCacheTimerId, 150, nullptr); // poll prefetch residency
        PollCacheStates();
    }

    // Update the frame readout without clobbering an in-progress edit.
    if (GetFocus() != m_seqFrameEdit)
    {
        wchar_t buf[16];
        swprintf_s(buf, L"%d", currentFrame);
        SetWindowTextW(m_seqFrameEdit, buf);
    }

    if (!wasActive || totalsChanged)
    {
        // Panel first appeared or frame count changed → full re-layout/repaint.
        LayoutControls();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    else
    {
        // Per-frame update (playback/step): just repaint the timeline so the
        // playhead moves, without a full layout (avoids flicker).
        InvalidateRect(m_hwnd, &m_timelineRect, FALSE);
    }
}

void Sidebar::ClearSequenceState()
{
    if (!m_seqActive)
        return;
    StopPlayback();
    KillTimer(m_hwnd, kSeqCacheTimerId);
    m_cacheStates.clear();
    m_seqActive = false;
    ShowWindow(m_seqFirstBtn, SW_HIDE);
    ShowWindow(m_seqPrevBtn, SW_HIDE);
    ShowWindow(m_seqNextBtn, SW_HIDE);
    ShowWindow(m_seqLastBtn, SW_HIDE);
    ShowWindow(m_seqFrameEdit, SW_HIDE);
    ShowWindow(m_seqPlayBtn, SW_HIDE);
    ShowWindow(m_seqFpsEdit, SW_HIDE);
    ShowWindow(m_seqLoopBtn, SW_HIDE);
    LayoutControls();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

// --- Playback ---------------------------------------------------------------

void Sidebar::UpdatePlayButtonLabel()
{
    SetWindowTextW(m_seqPlayBtn, m_playing ? L"Pause" : L"Play");
    InvalidateRect(m_seqPlayBtn, nullptr, FALSE);
}

void Sidebar::UpdateLoopButtonLabel()
{
    const wchar_t* label = (m_loopMode == kLoopOnce) ? L"Once" : (m_loopMode == kLoopPing) ? L"Ping" : L"Loop";
    SetWindowTextW(m_seqLoopBtn, label);
    InvalidateRect(m_seqLoopBtn, nullptr, FALSE);
}

void Sidebar::CommitFpsFromEdit()
{
    wchar_t buf[16] = {};
    GetWindowTextW(m_seqFpsEdit, buf, 16);
    int fps = _wtoi(buf);
    if (fps < 1)
        fps = 1;
    if (fps > 120)
        fps = 120;
    m_fps = fps;
    wchar_t norm[16];
    swprintf_s(norm, L"%d", fps);
    SetWindowTextW(m_seqFpsEdit, norm);
    if (m_playing) // re-anchor the cadence at the new rate
    {
        m_playStartMs = timeGetTime();
        m_playTickCount = 0;
        ScheduleNextPlayTick();
    }
}

void Sidebar::SetFps(int fps)
{
    if (fps < 1)
        fps = 1;
    if (fps > 120)
        fps = 120;
    m_fps = fps;
    wchar_t buf[16];
    swprintf_s(buf, L"%d", fps);
    SetWindowTextW(m_seqFpsEdit, buf);
}

void Sidebar::ResetPlaybackForTab(int fps, int loopMode)
{
    StopPlayback();
    SetFps(fps);
    m_loopMode = (loopMode >= 0 && loopMode <= 2) ? loopMode : kLoopLoop;
    UpdateLoopButtonLabel();
    if (onLoopModeChanged)
        onLoopModeChanged(m_loopMode); // update the cache's prefetch policy
}

void Sidebar::TogglePlayback()
{
    if (m_playing)
        StopPlayback();
    else
        StartPlayback();
}

// Query the prefetch cache and repaint the timeline if residency changed.
void Sidebar::PollCacheStates()
{
    if (!m_seqActive || !onQueryCacheStates)
        return;
    std::vector<uint8_t> states;
    onQueryCacheStates(states);
    if (states != m_cacheStates)
    {
        m_cacheStates = std::move(states);
        InvalidateRect(m_hwnd, &m_timelineRect, FALSE);
    }
}

// Shade the timeline track by prefetch residency: resident frames blend into the
// panel background, not-yet-loaded frames are amber, the frame decoding now is
// brighter amber. Drawn via NM_CUSTOMDRAW over the whole control rect.
void Sidebar::DrawTimelineCache(HDC hdc, const RECT& rc) const
{
    const COLORREF kResident = Colors::Background; // loaded — unobtrusive
    const COLORREF kPending = RGB(96, 74, 38);     // not loaded — muted amber
    const COLORREF kLoading = RGB(208, 150, 52);   // decoding now — bright amber

    const int width = rc.right - rc.left;
    const int total = m_seqTotalFrames; // authoritative frame count (snapshot may be stale)

    // Background — this also clears any previous playhead. Draw the residency
    // bands only when the snapshot matches the current sequence; right after a
    // tab switch it still holds the old sequence's states until the poll refreshes.
    if (total > 0 && static_cast<int>(m_cacheStates.size()) == total)
    {
        HBRUSH brushes[3] = {CreateSolidBrush(kPending), CreateSolidBrush(kLoading), CreateSolidBrush(kResident)};
        for (int i = 0; i < total; ++i)
        {
            RECT band = {rc.left + MulDiv(i, width, total), rc.top, rc.left + MulDiv(i + 1, width, total), rc.bottom};
            uint8_t st = m_cacheStates[i];
            FillRect(hdc, &band, brushes[st <= 2 ? st : 0]);
        }
        for (HBRUSH b : brushes)
            DeleteObject(b);
    }
    else
    {
        HBRUSH b = CreateSolidBrush(kResident);
        FillRect(hdc, &rc, b);
        DeleteObject(b);
    }

    // Border
    HPEN borderPen = CreatePen(PS_SOLID, 1, Colors::ButtonBorder);
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, borderPen));
    HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);

    // Playhead — a thin filled rect bounded inside the border (a pen LineTo to
    // rc.bottom would paint one pixel outside the fill area and leave a dot when
    // the playhead moves).
    if (total > 0)
    {
        int denom = (total > 1) ? (total - 1) : 1;
        int px = rc.left + MulDiv(m_seqCurrentIndex, width - 1, denom);
        int w = (std::max)(1, MulDiv(2, GetDpiForWindow(m_hwnd), 96));
        RECT head = {px, rc.top + 1, px + w, rc.bottom - 1};
        if (head.left < rc.left + 1)
            head.left = rc.left + 1;
        if (head.right > rc.right - 1)
            head.right = rc.right - 1;
        HBRUSH hb = CreateSolidBrush(Colors::TextBright);
        FillRect(hdc, &head, hb);
        DeleteObject(hb);
    }
}

void Sidebar::ScrubTimelineToX(int x)
{
    int total = m_seqTotalFrames;
    if (total <= 1)
        return;
    int width = m_timelineRect.right - m_timelineRect.left;
    if (width <= 0)
        return;
    int rel = x - m_timelineRect.left;
    rel = (std::max)(0, (std::min)(rel, width - 1));
    int idx = MulDiv(rel, total - 1, width - 1);
    int delta = idx - m_seqCurrentIndex;
    if (delta != 0 && onSequenceNav)
        onSequenceNav(delta);
}

void Sidebar::StartPlayback()
{
    if (m_playing || !m_seqActive || m_seqTotalFrames <= 1)
        return;
    // Once mode at the last frame would stop immediately — restart from frame 0.
    if (m_loopMode == kLoopOnce && m_seqCurrentIndex >= m_seqTotalFrames - 1 && onSequenceNav)
        onSequenceNav(-m_seqCurrentIndex);
    m_playing = true;
    m_playDir = 1;
    timeBeginPeriod(1); // raise timer resolution so the cadence can hit the target fps
    m_playStartMs = timeGetTime();
    m_playTickCount = 0;
    ScheduleNextPlayTick();
    UpdatePlayButtonLabel();
}

// Schedule the next playback tick so that tick N fires at anchor + N*(1000/fps),
// compensating for per-frame work and timer jitter. Re-anchors if we fall more
// than a frame behind, so a hiccup doesn't trigger a catch-up burst.
void Sidebar::ScheduleNextPlayTick()
{
    double frameMs = 1000.0 / m_fps;
    DWORD now = timeGetTime();
    double target = static_cast<double>(m_playStartMs) + (m_playTickCount + 1) * frameMs;
    double delay = target - static_cast<double>(now);
    if (delay < 1.0)
    {
        if (static_cast<double>(now) - target > frameMs)
        {
            m_playStartMs = now; // badly behind — drop the slip and restart the clock
            m_playTickCount = 0;
        }
        delay = 1.0;
    }
    SetTimer(m_hwnd, kSeqPlayTimerId, static_cast<UINT>(delay + 0.5), nullptr);
}

void Sidebar::StopPlayback()
{
    if (!m_playing)
        return;
    m_playing = false;
    KillTimer(m_hwnd, kSeqPlayTimerId);
    timeEndPeriod(1);
    UpdatePlayButtonLabel();
}

void Sidebar::PlayTick()
{
    if (!m_playing || m_seqTotalFrames <= 1)
        return;

    int last = m_seqTotalFrames - 1;
    int next = m_seqCurrentIndex + m_playDir;

    if (next > last)
    {
        if (m_loopMode == kLoopLoop)
            next = 0;
        else if (m_loopMode == kLoopPing)
        {
            m_playDir = -1;
            next = m_seqCurrentIndex - 1;
        }
        else // Once
        {
            StopPlayback();
            return;
        }
    }
    else if (next < 0) // ping-pong hitting the start
    {
        m_playDir = 1;
        next = m_seqCurrentIndex + 1;
    }

    int delta = next - m_seqCurrentIndex;
    if (delta != 0 && onSequenceNav)
        onSequenceNav(delta); // App advances, then calls back SetSequenceState

    if (m_playing) // StopPlayback (Once at end) may have cleared this
    {
        m_playTickCount++;
        ScheduleNextPlayTick();
    }
}

// True if two layer lists would produce the same listbox (same entries/order).
static bool SameLayerStructure(const ExrFileInfo& a, const ExrFileInfo& b)
{
    if (a.partCount != b.partCount || a.layers.size() != b.layers.size())
        return false;
    for (size_t i = 0; i < a.layers.size(); ++i)
    {
        const ExrLayer& x = a.layers[i];
        const ExrLayer& y = b.layers[i];
        if (x.name != y.name || x.partIndex != y.partIndex || x.mipLevel != y.mipLevel ||
            x.numMipLevels != y.numMipLevels)
            return false;
    }
    return true;
}

void Sidebar::SetLayers(const ExrFileInfo& info, int activeLayer)
{
    // Same structure as already shown (e.g. every frame during sequence playback)?
    // Skip the full listbox rebuild — it's wasteful and races with a user click
    // selecting a different layer mid-playback. Just sync the selection if needed.
    if (SameLayerStructure(info, m_layerInfo))
    {
        m_layerInfo = info; // keep stored copy current (channels, names)
        if (activeLayer != m_activeLayer)
        {
            m_activeLayer = activeLayer;
            if (activeLayer >= 0 && activeLayer < static_cast<int>(m_layerListMapping.size()))
            {
                m_suppressLayerChange = true;
                SendMessageW(m_layerList, LB_SETCURSEL, m_layerListMapping[activeLayer], 0);
                m_suppressLayerChange = false;
            }
        }
        return;
    }

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

    // 22 base controls + 4 playback (play, fps, loop, timeline) = 26
    HDWP hdwp = BeginDeferWindowPos(26);

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

    // --- Sequence panel (only when sequence is active) ---
    if (m_seqActive)
    {
        // "Sequence" label drawn in OnPaint
        y += labelH + MulDiv(2, dpi, 96);

        // [<<] [<] [  frame edit  ] [>] [>>]
        int navBtnW = MulDiv(28, dpi, 96);
        int editW = w - 2 * m - 4 * navBtnW;
        if (editW < MulDiv(40, dpi, 96))
            editW = MulDiv(40, dpi, 96);

        int x = m;
        hdwp = DeferWindowPos(hdwp, m_seqFirstBtn, nullptr, x, y, navBtnW, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += navBtnW;
        hdwp = DeferWindowPos(hdwp, m_seqPrevBtn, nullptr, x, y, navBtnW, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += navBtnW;
        hdwp = DeferWindowPos(hdwp, m_seqFrameEdit, nullptr, x, y, editW, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += editW;
        hdwp = DeferWindowPos(hdwp, m_seqNextBtn, nullptr, x, y, navBtnW, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += navBtnW;
        hdwp = DeferWindowPos(hdwp, m_seqLastBtn, nullptr, x, y, navBtnW, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);

        y += buttonH + MulDiv(4, dpi, 96);

        // Playback row: [Play] [fps] [loop-mode]
        int gap = MulDiv(4, dpi, 96);
        int totalW = w - 2 * m;
        int fpsW = MulDiv(40, dpi, 96);
        int loopW = MulDiv(48, dpi, 96);
        int playW = totalW - fpsW - loopW - 2 * gap;
        if (playW < MulDiv(40, dpi, 96))
            playW = MulDiv(40, dpi, 96);
        x = m;
        hdwp = DeferWindowPos(hdwp, m_seqPlayBtn, nullptr, x, y, playW, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += playW + gap;
        hdwp = DeferWindowPos(hdwp, m_seqFpsEdit, nullptr, x, y, fpsW, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);
        x += fpsW + gap;
        hdwp = DeferWindowPos(hdwp, m_seqLoopBtn, nullptr, x, y, m + totalW - x, buttonH, SWP_NOZORDER | SWP_NOACTIVATE);

        y += buttonH + gap;

        // Timeline scrubber row (full width) — custom-drawn in OnPaint; record its rect.
        m_timelineRect = {m, y, m + totalW, y + trackH};

        y += trackH + m;
    }
    else
    {
        m_timelineRect = {};
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

    // --- Sequence section (only when active) ---
    if (m_seqActive)
    {
        RECT seqLabelRect = {m, y, w - m, y + labelH};
        wchar_t seqLabel[64];
        swprintf_s(seqLabel, L"Sequence (%d frames)", m_seqTotalFrames);
        DrawTextW(memDC, seqLabel, -1, &seqLabelRect, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

        // The nav + playback button rows are child controls; the timeline is
        // custom-drawn here using its laid-out rect (shows prefetch residency).
        DrawTimelineCache(memDC, m_timelineRect);

        // Advance y past the whole sequence section to match LayoutControls:
        // label + nav row + playback row + timeline row.
        int buttonH = MulDiv(22, dpi, 96);
        int trackH = MulDiv(24, dpi, 96);
        y += labelH + MulDiv(2, dpi, 96);
        y += buttonH + MulDiv(4, dpi, 96);
        y += buttonH + MulDiv(4, dpi, 96);
        y += trackH + m;
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

    case WM_LBUTTONDOWN:
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (self->m_seqActive && PtInRect(&self->m_timelineRect, pt))
        {
            self->m_scrubbing = true;
            SetCapture(hwnd);
            self->ScrubTimelineToX(pt.x);
            return 0;
        }
        break;
    }

    case WM_MOUSEMOVE:
        if (self->m_scrubbing)
        {
            self->ScrubTimelineToX(GET_X_LPARAM(lParam));
            return 0;
        }
        break;

    case WM_LBUTTONUP:
        if (self->m_scrubbing)
        {
            self->m_scrubbing = false;
            ReleaseCapture();
            return 0;
        }
        break;

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
        else if (id == kSeqFirstBtnId && code == BN_CLICKED)
        {
            if (self->onSequenceFirst)
                self->onSequenceFirst();
        }
        else if (id == kSeqPrevBtnId && code == BN_CLICKED)
        {
            if (self->onSequenceNav)
                self->onSequenceNav(-1);
        }
        else if (id == kSeqNextBtnId && code == BN_CLICKED)
        {
            if (self->onSequenceNav)
                self->onSequenceNav(1);
        }
        else if (id == kSeqLastBtnId && code == BN_CLICKED)
        {
            if (self->onSequenceLast)
                self->onSequenceLast();
        }
        else if (id == kSeqFrameEditId && code == EN_KILLFOCUS)
        {
            // User finished editing the frame number — jump to it
            wchar_t buf[16] = {};
            GetWindowTextW(self->m_seqFrameEdit, buf, 16);
            int frameNum = _wtoi(buf);
            if (frameNum >= 0 && self->onSequenceJump)
                self->onSequenceJump(frameNum);
        }
        else if (id == kSeqPlayBtnId && code == BN_CLICKED)
        {
            if (self->m_playing)
                self->StopPlayback();
            else
                self->StartPlayback();
            // Drop focus so the button doesn't also eat the spacebar shortcut.
            SetFocus(self->m_parent);
        }
        else if (id == kSeqLoopBtnId && code == BN_CLICKED)
        {
            self->m_loopMode = (self->m_loopMode + 1) % 3; // Loop -> Once -> Ping
            self->UpdateLoopButtonLabel();
            if (self->onLoopModeChanged)
                self->onLoopModeChanged(self->m_loopMode);
            SetFocus(self->m_parent);
        }
        else if (id == kSeqFpsEditId && code == EN_KILLFOCUS)
        {
            self->CommitFpsFromEdit();
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

        // Owner-drawn sequence nav + playback buttons (same style as Auto)
        if ((dis->CtlID >= static_cast<UINT>(kSeqFirstBtnId) && dis->CtlID <= static_cast<UINT>(kSeqLastBtnId)) ||
            dis->CtlID == static_cast<UINT>(kSeqPlayBtnId) || dis->CtlID == static_cast<UINT>(kSeqLoopBtnId))
        {
            bool pressed = (dis->itemState & ODS_SELECTED) != 0;
            bool disabled = (dis->itemState & ODS_DISABLED) != 0;
            // Highlight the Play button while playback is active.
            bool active = (dis->CtlID == static_cast<UINT>(kSeqPlayBtnId) && self->m_playing);
            COLORREF bgColor = pressed ? Colors::ButtonPressed : (active ? Colors::Surface : Colors::Background);
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
            SetTextColor(dis->hDC, disabled ? Colors::TextDim : Colors::TextPrimary);
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

    case WM_TIMER:
        if (wParam == kSeqRepeatTimerId && self->m_seqRepeatDelta != 0)
        {
            // After the initial delay, switch to faster repeat rate
            SetTimer(hwnd, kSeqRepeatTimerId, kSeqRepeatMs, nullptr);
            if (self->onSequenceNav)
                self->onSequenceNav(self->m_seqRepeatDelta);
        }
        else if (wParam == kSeqPlayTimerId)
        {
            self->PlayTick();
        }
        else if (wParam == kSeqCacheTimerId)
        {
            self->PollCacheStates();
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_CTLCOLOREDIT:
    {
        HDC childDC = reinterpret_cast<HDC>(wParam);
        SetBkColor(childDC, Colors::Surface);
        SetTextColor(childDC, Colors::TextPrimary);
        static HBRUSH surfaceBrush = nullptr;
        if (!surfaceBrush)
            surfaceBrush = CreateSolidBrush(Colors::Surface);
        return reinterpret_cast<LRESULT>(surfaceBrush);
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
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
