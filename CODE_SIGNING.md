# Code Signing Policy

EXRay release binaries will be signed, but are not yet. The remainder of this document describes the intended end-state.

## Signed artifacts

Every release published from the `main` branch produces two signed artifacts:

| Artifact | Description |
|---|---|
| `EXRay-vX.Y.Z-win64.zip` | Portable binary |
| `EXRay-X.Y.Z-setup.exe` | Inno Setup installer |

Only binaries built by the project's GitHub Actions CI pipeline are signed.
No locally-built binaries are ever signed with the release certificate.

## Build and signing process

1. A maintainer pushes a version tag (`vX.Y.Z`) from the `main` branch.
2. GitHub Actions builds the optimized binary and installer.
3. Artifacts are submitted to SignPath for signing.
4. Signed artifacts are attached to the GitHub Release.

Source code is publicly available at <https://github.com/hughes/EXRay> and all
builds are reproducible from the corresponding git tag.

## Team roles

| Role | Member | GitHub |
|---|---|---|
| Author | Matt Hughes | [@hughes](https://github.com/hughes) |
| Reviewer | Matt Hughes | [@hughes](https://github.com/hughes) |
| Approver | Matt Hughes | [@hughes](https://github.com/hughes) |

All team members have multi-factor authentication enabled.

External contributions are reviewed and approved by a Reviewer before merging.
Only an Approver can authorize a release for signing.

## Security practices

- All commits to `main` are reviewed before merge.
- CI builds are the sole source of signed binaries — the signing key is never
  available locally.
- Upstream dependencies (OpenEXR, etc.) are included unsigned and are not signed
  with the project certificate.

## Privacy

EXRay does not collect, transmit, or store any user data. It includes no
telemetry or analytics. The only network request is an optional update check
against the GitHub Releases API — no personal or usage data is sent. See
[Privacy](README.md#privacy) in the README for details.
