# EXRay

A fast, native EXR image viewer for Windows. Hardware-accelerated, HDR-aware, and built for people who just want to see their images.

<!-- TODO: hero screenshot or GIF here -->

## Features

- **Instant loading** - D3D11 hardware-accelerated rendering with background preload for adjacent tabs
- **HDR display support** - auto-detects HDR-capable monitors and outputs scRGB linear
- **Exposure & gamma control** - adjust EV stops and gamma in real time with keyboard or scroll wheel
- **Histogram overlay** - 512-bin histogram with per-channel or luminance display
- **Pixel inspector** - live RGBA readout under cursor in the status bar
- **Pixel grid** - sub-pixel grid overlay that fades in at high zoom levels
- **Multi-image tabs** - open multiple EXR files with smart memory management (adjacent tabs stay cached)
- **Drag and drop** - drop `.exr` files onto the window to open them
- **Fullscreen** - borderless fullscreen on the current monitor

## Controls and Hotkeys

| Action | Input |
|---|---|
| Open file | `Ctrl+O` |
| Reload | `Ctrl+R` |
| Close tab | `Ctrl+W` |
| Next / previous tab | `Ctrl+Tab` / `Ctrl+Shift+Tab` |
| Fit to window | `Ctrl+0` |
| Actual size (1:1) | `Ctrl+1` |
| Zoom | Scroll wheel (centered on cursor) |
| Pan | Middle mouse drag |
| Exposure ±0.25 EV | `+` / `-` or `Ctrl+Scroll` |
| Gamma ±  (SDR) | `]` / `[` |
| Toggle histogram | `H` |
| Cycle histogram channel | `C` |
| Toggle pixel grid | `G` |
| Toggle fullscreen | `F11` |
| Exit fullscreen | `Esc` |

## Channel modes

The histogram and pixel grid support five channel views, cycled with `C`:

**Luminance** · **Red** · **Green** · **Blue** · **All (RGBA)**

## Installation

### Portable

Download the latest zip from [Releases](https://github.com/hughes/EXRay/releases), extract, and run `EXRay.exe`. No installer required - single standalone executable with no runtime dependencies.

### Build from source

EXRay uses [Bazel](https://bazel.build/) with MSVC on Windows.

```
git clone https://github.com/hughes/EXRay.git
cd EXRay
bazelisk build //:EXRay
```

The built binary is at `bazel-bin/EXRay.exe`.

**Requirements:** Windows 10+, MSVC (Visual Studio 2022 or Build Tools), Windows SDK, [Bazelisk](https://github.com/bazelbuild/bazelisk).

## License

EXRay is licensed under the [GNU General Public License v3.0](LICENSE).

Uses [OpenEXR](https://openexr.com/) (BSD-3-Clause) - see [THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES).

## Links

- [Changelog](CHANGELOG.md)
- [Matt Hughes](https://github.com/hughes)
