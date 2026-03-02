# EXRay Release Checklist

## How to release

1. **Bump the version** in these files (keep them in sync):
   - `src/resource.h` — `EXRAY_VERSION_STR`, `EXRAY_VERSION_MAJOR/MINOR/PATCH` (source of truth)
   - `MODULE.bazel` — `version` field
   - `installer/EXRay.iss` — `MyAppVersion` (also overridden by CI from the git tag)
2. **Update `CHANGELOG.md`** — replace "Unreleased" with the version and date
3. **Commit**: `git commit -am "Prepare vX.Y.Z release"`
4. **Tag**: `git tag vX.Y.Z`
5. **Push**: `git push origin main --tags`

CI handles the rest. The release workflow (`.github/workflows/release.yml`):
- Builds an optimized binary (`bazelisk build -c opt`)
- Packages a portable zip (`EXRay-vX.Y.Z-win64.zip`)
- Builds the Inno Setup installer (`EXRay-X.Y.Z-setup.exe`)
- Creates a GitHub Release with both artifacts and auto-generated notes

The workflow only runs for tags pushed from `main`.

### Release artifacts

| File | Description |
|---|---|
| `EXRay-vX.Y.Z-win64.zip` | Portable — unzip and run, no install needed |
| `EXRay-X.Y.Z-setup.exe` | Installer — Start menu, file association, uninstaller |

---

## Pre-release checklist

## 1. Polish & Completeness

- [x] About dialog — version string, links, license info
- [x] File association — register as handler for `.exr` files (via Inno Setup installer)
- [x] Drag-and-drop — drop `.exr` files onto window to open
- [x] Error handling UX — dimension sanity check, OOM message, catch-all for unknown exceptions
- [x] Recent files in File menu
- [x] Window title shows current filename
- [x] Embed `VERSIONINFO` resource in RC file (version, copyright, description)
- [ ] Investigate scroll stutter fix (DirectComposition swap chain or other approach from BUGS.md)
- [ ] Update check — background WinHTTP GET to `api.github.com/repos/hughes/EXRay/releases/latest`, compare `tag_name` semver to current version. If newer: asterisk on Help menu, "Update available" line in About dialog. No interruptions. Requires at least one published release to test against.
- [x] Make sure all environment-specific stuff referencing the local development paths is removed.

## 2. Testing

- [x] Manual test matrix — open various EXR files (single-part, multi-part, tiled, deep, different compressions, huge, tiny, malformed)
- [x] Manual test matrix — zoom, pan, exposure, histogram, tabs, fullscreen
- [x] Manual test matrix — HDR display, non-HDR display, multi-monitor, high-DPI
- [x] Crash/fuzz resilience — garbage and edge-case EXR files don't crash or hang (validated by `--validate` against 3 fuzzed + 5 graceful-failure files)
- [x] Multi-GPU/driver testing — NVIDIA, AMD, Intel (verified NVIDIA, Intel)
- [x] Performance benchmarks — time to open/render various file sizes (`--benchmark` flag, JSON output, CI artifact, `show_benchmark.sh` / `compare_benchmark.sh`)
- [x] Automated headless validation — `EXRay.exe --validate tests/images/` (load + histogram on ~50 OpenEXR test images, CI-friendly)
- [x] Automated GUI smoke test — `tests/smoke_test.sh` launches app per file, verifies window title + no crash (requires display)

## 3. Licensing

- [x] Pick a license — GPLv3
- [x] Add `LICENSE` file to repo root
- [x] Verify OpenEXR license (BSD-3-Clause) compatibility — confirmed, added `THIRD_PARTY_LICENSES`
- [x] Add license headers or NOTICE file as needed

## 4. Build & CI/CD

- [x] GitHub Actions workflow — build on push/PR (Bazel + MSVC + Windows SDK)
- [x] Cache Bazel build artifacts in CI
- [x] Release workflow — triggered by git tag, builds optimized binary, creates GitHub Release with zip
- [x] Single source of truth for version — `resource.h` defines feed VERSIONINFO + About dialog (still need to sync with MODULE.bazel)

## 5. Code Signing (post-release — requires published OSS project)

- [ ] Apply to [SignPath.io](https://signpath.io/) for free OSS code signing (requires public repo + release history)
- [ ] Integrate signing into release pipeline
- [ ] Verify SmartScreen doesn't block the signed binary
- Note: v0.1.0 ships unsigned. SmartScreen will warn but users can click through.

## 6. GitHub Releases

- [x] Release CI produces standalone `EXRay.exe` (unsigned for v0.1.0; signing added post-release)
- [x] Verify no runtime DLL dependencies beyond system libs — all system DLLs + MSVC runtime (bundle VC++ Redist or statically link with `/MT`)
- [x] Create `CHANGELOG.md`
- [x] Provide both portable `.zip` and installer — Inno Setup `.iss` in `installer/`

## 7. Windows Installer

- [x] Choose installer format — Inno Setup for GitHub/WinGet (MSIX separate for Store)
- [x] Start menu shortcut
- [x] `.exr` file association (user-selectable task)
- [x] Uninstall entry in Add/Remove Programs
- [x] Optional: "Open with" context menu integration

## 8. WinGet (Windows Package Manager)

- [ ] Create manifest YAML for [winget-pkgs](https://github.com/microsoft/winget-pkgs)
- [ ] Stable GitHub Release download URL + SHA256 hash
- [ ] Submit PR to winget-pkgs repo
- [ ] Automate future updates with `wingetcreate` (stretch goal)

## 9. Microsoft Store

- [x] Register Microsoft Partner Center account (Free for individuals)
- [ ] Create `Package.appxmanifest` (app identity, capabilities, file type associations)
- [ ] App icon at required sizes (44x44, 150x150, 300x300)
- [ ] Store screenshots (3-5 showing key features)
- [ ] Store listing — short description, long description, category, age rating
- [ ] Privacy policy URL
- [ ] Build MSIX with Microsoft publisher identity
- [ ] Submit for certification (1-3 business day review)

## 10. Documentation

- [ ] Polish `README.md` — feature list, screenshots/GIF, build instructions, download link
- [x] Keyboard shortcuts / controls reference
- [x] Supported EXR features and limitations — added to README.md

## Suggested Priority Order

Sections 1-4 and 6 get you to a credible public GitHub release. Section 10 (at least the README) should land before the first release. Section 5 (code signing) requires a published OSS project, so it comes after v0.1.0. Sections 7-9 fulfill the full distribution promise. The scroll stutter, update check, and missing UX features can be addressed in parallel throughout.
