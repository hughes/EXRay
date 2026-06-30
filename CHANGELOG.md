# Changelog

## 0.4.0 - 2026-06-30

- Image sequences — auto-detects numbered EXR frames; step through with `←` / `→` or `Home` / `End`
- Sequence playback — play/pause (`Space`), adjustable frame rate, and loop / play-once / ping-pong end modes
- Background prefetching — sequence frames are decoded into RAM for smooth, near-instant playback; a scrubbable timeline shades buffering progress
- Frames cached across all open sequences and layers under a shared memory budget with least-recently-used eviction, so switching tabs keeps sequences warm
- Compare mode — Split, Difference, Add, or Over blending of any two images, layers, or frames
- Per-layer exposure — each layer (e.g. RGB vs. Z depth) keeps its own auto-computed exposure
- Faster loading — pixels are stored natively as 16-bit half and decoded with OpenEXR multithreading; frame steps and layer switches are served from the cache as a GPU upload
- Recent files list one entry per sequence instead of one per opened frame
- Fixes — switching layers during playback now sticks; new images auto-expose instead of inheriting the previous tab's exposure; timeline playhead no longer flickers or leaves a stray pixel when switching tabs

## 0.3.2 - 2026-03-14

- Fix XYZ channel identification
- Remove tab preloading for simpler tab lifecycle
- Code formatting standardization

## 0.3.1 - 2026-03-14

- Fix Help menu text rendering as black-on-dark in dark mode when update is available

## 0.3.0 - 2026-03-14

- Sidebar with layer hierarchy and channel controls
- Multi-layer EXR support (view and switch between layers/parts)
- Mipmap level viewing
- Multi-channel and single-channel inspection modes
- "No alpha" display mode with auto-detection for images that need it
- Chromaticities premultiplication for correct color rendering
- Middle-click to close tabs
- GUI theming system with improved DPI scaling
- Reworked histogram channel buttons and color summation
- Adjusted scroll zoom and exposure behavior

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
