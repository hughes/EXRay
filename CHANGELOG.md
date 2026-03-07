# Changelog

## 0.2.0 - 2026-03-07

- Add Windows Explorer thumbnail previews for EXR files (shell extension DLL)

## 0.1.1 - 2026-03-06

- Add update checking
- Fix stutter issue with high inertia scroll wheel

## 0.1.0 - 2026-03-01

Initial release.

- Native Win32 EXR viewer with D3D11.1 hardware-accelerated rendering
- Tone mapping with exposure and gamma controls
- Auto-exposure based on histogram analysis
- HDR display output support (SDR/HDR toggle)
- Histogram overlay with per-channel display (Luminance, R, G, B, All)
- Pixel grid overlay at high zoom
- Multi-file tabs with sliding-window memory cache and background preloading
- Touchpad and touchscreen support (pinch-to-zoom, pan gestures)
- Drag-and-drop file opening
- Single-instance mode - opening a second file activates the existing window
- Recent files menu
- Fullscreen mode (F11)
- Pixel value inspection (coords + RGBA in status bar)
- Fit-to-window and actual-size zoom presets
- High-DPI and multi-monitor display change handling
