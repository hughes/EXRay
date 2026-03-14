// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>

// Dynamic color palette — populated by Theme::Refresh() based on system theme.
// All values are set during Theme::Init(); do not use before Init() is called.
namespace Colors
{
inline COLORREF Background = 0;
inline COLORREF Surface = 0;
inline COLORREF TabActive = 0;
inline COLORREF TextPrimary = 0;
inline COLORREF TextBright = 0;
inline COLORREF TextDim = 0;
inline COLORREF Selection = 0;
inline COLORREF Connector = 0;
inline COLORREF Separator = 0;
inline COLORREF ButtonHover = 0;
inline COLORREF ButtonPressed = 0;
inline COLORREF ButtonBorder = 0;
inline COLORREF SelectionText = 0;
inline COLORREF FocusBorder = 0;
} // namespace Colors

namespace Theme
{
// Call once at startup after SetProcessDpiAwarenessContext.
// Resolves undocumented uxtheme dark mode APIs and sets initial theme from system.
void Init();

// Returns true if the app is currently using dark mode (follows system preference).
bool IsDark();

// Re-read the system theme preference and update Colors:: values and background brush.
// Call this in response to WM_SETTINGCHANGE with "ImmersiveColorSet".
void Refresh();

// Enable dark/light title bar and non-client area for a top-level window.
void EnableForWindow(HWND hwnd);

// Apply current theme to a common control (trackbar, listbox, etc.).
void ApplyToControl(HWND hwnd);

// Apply current theme specifically for combo box controls.
void ApplyToComboBox(HWND hwnd);

// Create a DPI-scaled Segoe UI font for the given window's DPI.
// Caller owns the returned HFONT and must call DeleteObject when done.
HFONT CreateDpiFont(HWND hwnd, int pointSize = 9);

// Shared background brush — recreated on theme change.
// Do NOT DeleteObject — lifetime is process-wide.
HBRUSH GetBackgroundBrush();
} // namespace Theme
