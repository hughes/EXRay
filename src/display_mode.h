// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Display mode constants: 0=RGB, 1=R, 2=G, 3=B, 4=A, 5=RGB(ignore alpha)
constexpr int kDisplayModeRGB = 0;
constexpr int kDisplayModeR = 1;
constexpr int kDisplayModeG = 2;
constexpr int kDisplayModeB = 3;
constexpr int kDisplayModeA = 4;
constexpr int kDisplayModeRGBNoAlpha = 5;

// Given the current display mode and whether the loaded image has all-zero
// alpha, return the display mode that should be active.
//
// Auto-switches between RGB (0) and RGB-ignore-alpha (5):
//   - all-zero alpha + currently RGB  →  switch to ignore-alpha
//   - meaningful alpha + currently ignore-alpha  →  switch back to RGB
//   - any other mode (solo channel)  →  leave unchanged
//
// For new file loads, pass currentMode = kDisplayModeRGB so the auto-select
// always kicks in.
inline int AutoSelectDisplayMode(int currentMode, bool alphaAllZero)
{
    if (alphaAllZero && currentMode == kDisplayModeRGB)
        return kDisplayModeRGBNoAlpha;
    if (!alphaAllZero && currentMode == kDisplayModeRGBNoAlpha)
        return kDisplayModeRGB;
    return currentMode;
}
