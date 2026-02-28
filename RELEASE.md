# EXRay Release Checklist

## 1. Polish & Completeness

- [x] About dialog — version string, links, license info
- [ ] File association — register as handler for `.exr` files
- [x] Drag-and-drop — drop `.exr` files onto window to open
- [x] Error handling UX — dimension sanity check, OOM message, catch-all for unknown exceptions
- [x] Recent files in File menu
- [x] Window title shows current filename
- [x] Embed `VERSIONINFO` resource in RC file (version, copyright, description)
- [ ] Investigate scroll stutter fix (DirectComposition swap chain or other approach from BUGS.md)
- [ ] Update check — background WinHTTP GET to `api.github.com/repos/hughes/EXRay/releases/latest`, compare `tag_name` semver to current version. If newer: asterisk on Help menu, "Update available" line in About dialog. No interruptions. Requires at least one published release to test against.

## 2. Testing

- [ ] Manual test matrix — open various EXR files (single-part, multi-part, tiled, deep, different compressions, huge, tiny, malformed)
- [ ] Manual test matrix — zoom, pan, exposure, histogram, tabs, fullscreen
- [ ] Manual test matrix — HDR display, non-HDR display, multi-monitor, high-DPI
- [ ] Crash/fuzz resilience — garbage and edge-case EXR files don't crash or hang
- [ ] Multi-GPU/driver testing — NVIDIA, AMD, Intel
- [ ] Performance benchmarks — time to open/render various file sizes (back up "insanely fast" claim)
- [ ] Automated smoke test in CI (stretch goal)

## 3. Licensing

- [x] Pick a license — GPLv3
- [x] Add `LICENSE` file to repo root
- [x] Verify OpenEXR license (BSD-3-Clause) compatibility — confirmed, added `THIRD_PARTY_LICENSES`
- [ ] Add license headers or NOTICE file as needed

## 4. Build & CI/CD

- [x] GitHub Actions workflow — build on push/PR (Bazel + MSVC + Windows SDK)
- [x] Cache Bazel build artifacts in CI
- [x] Release workflow — triggered by git tag, builds optimized binary, creates GitHub Release with zip
- [x] Single source of truth for version — `resource.h` defines feed VERSIONINFO + About dialog (still need to sync with MODULE.bazel)

## 5. Code Signing

- [ ] Obtain code signing certificate (SignPath for free OSS, or paid EV cert)
- [ ] Integrate signing into release pipeline
- [ ] Verify SmartScreen doesn't block the signed binary

## 6. GitHub Releases

- [ ] Release CI produces signed standalone `EXRay.exe`
- [x] Verify no runtime DLL dependencies beyond system libs — all system DLLs + MSVC runtime (bundle VC++ Redist or statically link with `/MT`)
- [x] Create `CHANGELOG.md`
- [ ] Provide both portable `.zip` and installer

## 7. Windows Installer

- [ ] Choose installer format (MSIX for Store, plus traditional WiX/Inno Setup for GitHub/WinGet)
- [ ] Start menu shortcut
- [ ] `.exr` file association
- [ ] Uninstall entry in Add/Remove Programs
- [ ] Optional: "Open with" context menu integration

## 8. WinGet (Windows Package Manager)

- [ ] Create manifest YAML for [winget-pkgs](https://github.com/microsoft/winget-pkgs)
- [ ] Stable GitHub Release download URL + SHA256 hash
- [ ] Submit PR to winget-pkgs repo
- [ ] Automate future updates with `wingetcreate` (stretch goal)

## 9. Microsoft Store

- [ ] Register Microsoft Partner Center account (~$19)
- [ ] Create `Package.appxmanifest` (app identity, capabilities, file type associations)
- [ ] App icon at required sizes (44x44, 150x150, 300x300 — currently only 32x32)
- [ ] Store screenshots (3-5 showing key features)
- [ ] Store listing — short description, long description, category, age rating
- [ ] Privacy policy URL
- [ ] Build MSIX with Microsoft publisher identity
- [ ] Submit for certification (1-3 business day review)

## 10. Documentation

- [ ] Polish `README.md` — feature list, screenshots/GIF, build instructions, download link
- [ ] GitHub Pages site or landing page (e.g. `hughes.github.io/EXRay`)
- [ ] Keyboard shortcuts / controls reference
- [ ] Supported EXR features and limitations

## Suggested Priority Order

Sections 1-6 get you to a credible public GitHub release. Section 10 (at least the README) should land before the first release. Sections 7-9 fulfill the full distribution promise. The scroll stutter and missing UX features can be addressed in parallel throughout.
