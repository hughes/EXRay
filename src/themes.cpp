// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "themes.h"

#include <dwmapi.h>
#include <uxtheme.h>

// Undocumented uxtheme ordinals for Windows 10/11 dark mode
// These have been stable since 1809 and are used by Firefox, VS Code, etc.
using fnAllowDarkModeForWindow = BOOL(WINAPI*)(HWND, BOOL);
using fnSetPreferredAppMode = int(WINAPI*)(int);
using fnFlushMenuThemes = void(WINAPI*)();

static fnAllowDarkModeForWindow pAllowDarkModeForWindow = nullptr;
static fnSetPreferredAppMode pSetPreferredAppMode = nullptr;
static fnFlushMenuThemes pFlushMenuThemes = nullptr;

static HBRUSH s_bgBrush = nullptr;
static bool s_isDark = true;

static bool QuerySystemDarkMode()
{
    DWORD value = 1; // default: light theme
    DWORD size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_DWORD, nullptr, &value, &size);
    return value == 0; // 0 = dark mode enabled
}

void Theme::Init()
{
    HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll");
    if (!uxtheme)
        return;

    // Ordinal 130: AllowDarkModeForWindow (1809+)
    pAllowDarkModeForWindow =
        reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(130)));

    // Ordinal 135: SetPreferredAppMode (1903+)
    // Falls back to ordinal 133: AllowDarkModeForApp (1809)
    pSetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));

    if (!pSetPreferredAppMode)
    {
        // 1809 fallback: ordinal 133
        using fnAllowDarkModeForApp = BOOL(WINAPI*)(BOOL);
        auto pAllow = reinterpret_cast<fnAllowDarkModeForApp>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(133)));
        if (pAllow)
            pAllow(TRUE);
    }
    else
    {
        // AllowDark = 1 (follows system dark/light preference)
        pSetPreferredAppMode(1);
    }

    // Ordinal 136: FlushMenuThemes
    pFlushMenuThemes = reinterpret_cast<fnFlushMenuThemes>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));

    // Set initial colors based on system theme
    Refresh();
}

bool Theme::IsDark() { return s_isDark; }

void Theme::Refresh()
{
    s_isDark = QuerySystemDarkMode();

    if (s_isDark)
    {
        Colors::Background = RGB(0x2D, 0x2D, 0x2D);
        Colors::Surface = RGB(0x1A, 0x1A, 0x1A);
        Colors::TabActive = RGB(0x3A, 0x3A, 0x3A);
        Colors::TextPrimary = RGB(0xCC, 0xCC, 0xCC);
        Colors::TextBright = RGB(0xFF, 0xFF, 0xFF);
        Colors::TextDim = RGB(0x99, 0x99, 0x99);
        Colors::Selection = RGB(0x3A, 0x5A, 0x8A);
        Colors::Connector = RGB(0x66, 0x66, 0x66);
        Colors::Separator = RGB(0x44, 0x44, 0x44);
        Colors::ButtonHover = RGB(0x45, 0x45, 0x45);
        Colors::ButtonPressed = RGB(0x1E, 0x1E, 0x1E);
        Colors::ButtonBorder = RGB(0x66, 0x66, 0x66);
        Colors::SelectionText = RGB(0xFF, 0xFF, 0xFF);
        Colors::FocusBorder = RGB(0x3A, 0x5A, 0x8A);
    }
    else
    {
        Colors::Background = GetSysColor(COLOR_WINDOW);
        Colors::Surface = GetSysColor(COLOR_BTNFACE);
        Colors::TabActive = GetSysColor(COLOR_WINDOW);
        Colors::TextPrimary = GetSysColor(COLOR_WINDOWTEXT);
        Colors::TextBright = GetSysColor(COLOR_WINDOWTEXT);
        Colors::TextDim = GetSysColor(COLOR_GRAYTEXT);
        Colors::Selection = GetSysColor(COLOR_HIGHLIGHT);
        Colors::SelectionText = GetSysColor(COLOR_HIGHLIGHTTEXT);
        Colors::Connector = GetSysColor(COLOR_GRAYTEXT);
        Colors::Separator = GetSysColor(COLOR_3DLIGHT);
        Colors::ButtonHover = GetSysColor(COLOR_BTNHIGHLIGHT);
        Colors::ButtonPressed = GetSysColor(COLOR_BTNSHADOW);
        Colors::ButtonBorder = GetSysColor(COLOR_BTNSHADOW);
        Colors::FocusBorder = GetSysColor(COLOR_HIGHLIGHT);
    }

    // Recreate the background brush
    if (s_bgBrush)
        DeleteObject(s_bgBrush);
    s_bgBrush = CreateSolidBrush(Colors::Background);

    // Flush menus so they pick up the new theme
    if (pFlushMenuThemes)
        pFlushMenuThemes();
}

void Theme::EnableForWindow(HWND hwnd)
{
    if (pAllowDarkModeForWindow)
        pAllowDarkModeForWindow(hwnd, s_isDark ? TRUE : FALSE);

    // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 20H1+)
    // Earlier builds used attribute 19
    BOOL useDark = s_isDark ? TRUE : FALSE;
    HRESULT hr = DwmSetWindowAttribute(hwnd, 20, &useDark, sizeof(useDark));
    if (FAILED(hr))
        DwmSetWindowAttribute(hwnd, 19, &useDark, sizeof(useDark));
}

void Theme::ApplyToControl(HWND hwnd)
{
    if (pAllowDarkModeForWindow)
        pAllowDarkModeForWindow(hwnd, s_isDark ? TRUE : FALSE);
    SetWindowTheme(hwnd, s_isDark ? L"DarkMode_Explorer" : nullptr, nullptr);
}

void Theme::ApplyToComboBox(HWND hwnd)
{
    if (pAllowDarkModeForWindow)
        pAllowDarkModeForWindow(hwnd, s_isDark ? TRUE : FALSE);
    SetWindowTheme(hwnd, s_isDark ? L"DarkMode_CFD" : nullptr, nullptr);

    // Also theme the dropdown list child (if CBS_DROPDOWNLIST)
    COMBOBOXINFO cbi = {sizeof(cbi)};
    if (GetComboBoxInfo(hwnd, &cbi))
    {
        if (cbi.hwndList)
        {
            if (pAllowDarkModeForWindow)
                pAllowDarkModeForWindow(cbi.hwndList, s_isDark ? TRUE : FALSE);
            SetWindowTheme(cbi.hwndList, s_isDark ? L"DarkMode_CFD" : nullptr, nullptr);
        }
    }
}

HFONT Theme::CreateDpiFont(HWND hwnd, int pointSize)
{
    int dpi = GetDpiForWindow(hwnd);
    int height = -MulDiv(pointSize, dpi, 72);

    return CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

HBRUSH Theme::GetBackgroundBrush()
{
    if (!s_bgBrush)
        s_bgBrush = CreateSolidBrush(Colors::Background);
    return s_bgBrush;
}
