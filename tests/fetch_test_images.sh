#!/usr/bin/env bash
# fetch_test_images.sh — Download OpenEXR test images for EXRay testing.
# Images are placed in tests/images/ which is .gitignored.
# Re-running is safe: skips download if images are already present.
#
# Downloads the repo as a single zip archive (one HTTP request),
# then extracts only the files we need.
#
# Usage:
#   ./tests/fetch_test_images.sh            # fetch all categories
#   ./tests/fetch_test_images.sh edge       # fetch only one category
#   ./tests/fetch_test_images.sh --list     # show available categories
#   ./tests/fetch_test_images.sh --clean    # remove images and cached archive

set -euo pipefail

# Pin to a specific commit for reproducible test results.
COMMIT="e38ffb0790f62f05a6f083a6fa4cac150b3b7452"
ARCHIVE_URL="https://github.com/AcademySoftwareFoundation/openexr-images/archive/${COMMIT}.zip"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$SCRIPT_DIR/images"
CACHE_ZIP="$SCRIPT_DIR/.openexr-images.zip"
# Prefix inside the zip (GitHub archives as reponame-<sha>/).
ZIP_PREFIX="openexr-images-${COMMIT}"

# ---------------------------------------------------------------------------
# Test image manifest — organised by testing purpose
# ---------------------------------------------------------------------------

declare -A CATEGORIES
CATEGORIES=(
    [scanline]="Basic scanline images (various compressions)"
    [tiled]="Tiled images"
    [lumachroma]="Luminance/chroma images"
    [edge]="Edge cases: NaN, Inf, wide gamut, extreme range"
    [displaywindow]="Display/data window offset and cropping"
    [multipart]="Multi-part files (expect graceful failure)"
    [multiview]="Multi-view/stereo files (expect graceful failure)"
    [deep]="Deep data files (expect graceful failure)"
    [multiresolution]="Multi-resolution mip-mapped and rip-mapped images"
    [damaged]="Fuzzed/corrupt files for crash resilience"
)

scanline_files=(
    "ScanLines/Desk.exr"
    "ScanLines/Cannon.exr"
    "ScanLines/StillLife.exr"
    "ScanLines/CandleGlass.exr"
    "ScanLines/PrismsLenses.exr"
    "ScanLines/Carrots.exr"
    "ScanLines/Tree.exr"
    "ScanLines/MtTamWest.exr"
    "ScanLines/Blobbies.exr"
)

tiled_files=(
    "Tiles/GoldenGate.exr"
    "Tiles/Spirals.exr"
    "Tiles/Ocean.exr"
)

lumachroma_files=(
    "LuminanceChroma/Flowers.exr"
    "LuminanceChroma/Garden.exr"
    "LuminanceChroma/CrissyField.exr"
    "LuminanceChroma/StarField.exr"
)

edge_files=(
    "TestImages/AllHalfValues.exr"
    "TestImages/BrightRings.exr"
    "TestImages/BrightRingsNanInf.exr"
    "TestImages/WideColorGamut.exr"
    "TestImages/WideFloatRange.exr"
    "TestImages/SquaresSwirls.exr"
    "TestImages/GammaChart.exr"
    "TestImages/GrayRampsHorizontal.exr"
    "TestImages/RgbRampsDiagonal.exr"
)

displaywindow_files=(
    "DisplayWindow/t01.exr"
    "DisplayWindow/t02.exr"
    "DisplayWindow/t03.exr"
    "DisplayWindow/t04.exr"
    "DisplayWindow/t05.exr"
    "DisplayWindow/t06.exr"
    "DisplayWindow/t07.exr"
    "DisplayWindow/t08.exr"
    "DisplayWindow/t09.exr"
    "DisplayWindow/t10.exr"
    "DisplayWindow/t11.exr"
    "DisplayWindow/t12.exr"
    "DisplayWindow/t13.exr"
    "DisplayWindow/t14.exr"
    "DisplayWindow/t15.exr"
    "DisplayWindow/t16.exr"
)

multipart_files=(
    "Beachball/multipart.0001.exr"
    "Beachball/singlepart.0001.exr"
)

multiview_files=(
    "MultiView/Adjuster.exr"
    "MultiView/Balls.exr"
    "MultiView/Fog.exr"
)

deep_files=(
    "v2/Stereo/Balls.exr"
    "v2/Stereo/composited.exr"
)

multiresolution_files=(
    "MultiResolution/ColorCodedLevels.exr"
    "MultiResolution/MirrorPattern.exr"
    "MultiResolution/KernerEnvCube.exr"
    "MultiResolution/OrientationCube.exr"
    "MultiResolution/OrientationLatLong.exr"
    "MultiResolution/PeriodicPattern.exr"
    "MultiResolution/WavyLinesCube.exr"
    "MultiResolution/StageEnvCube.exr"
)

damaged_files=(
    "Damaged/asan_heap-oob_4cb169_255_cc7ac9cde4b8634b31cb41c8fe89b92d_exr"
    "Damaged/asan_heap-oob_4cb169_380_4572f174dd4e48b879ca6d516486f30e_exr"
    "Damaged/asan_heap-oob_4cb169_978_5f00ce89c3847e739b256efc49f312cf_exr"
)

ALL_CATEGORIES=(scanline tiled multiresolution lumachroma edge displaywindow multipart multiview deep damaged)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

list_categories() {
    echo "Available categories:"
    for cat in "${ALL_CATEGORIES[@]}"; do
        printf "  %-16s %s\n" "$cat" "${CATEGORIES[$cat]}"
    done
}

ensure_zip() {
    if [[ -f "$CACHE_ZIP" ]]; then
        return
    fi
    echo "Downloading openexr-images archive (single HTTP request)..."
    if ! curl -fSL --retry 2 -o "$CACHE_ZIP" "$ARCHIVE_URL"; then
        echo "FAIL: could not download archive."
        rm -f "$CACHE_ZIP"
        exit 1
    fi
    echo "Download complete."
}

# Extract requested files from the zip into OUT_DIR.
# Accepts an array of paths relative to repo root (e.g. "ScanLines/Desk.exr").
extract_files() {
    local -n paths="$1"
    local extracted=0 skipped=0

    # Build list of zip entries to extract (only files not already present).
    local entries=()
    for f in "${paths[@]}"; do
        if [[ -f "$OUT_DIR/$f" ]]; then
            skipped=$((skipped + 1))
        else
            entries+=("$ZIP_PREFIX/$f")
        fi
    done

    if [[ ${#entries[@]} -eq 0 ]]; then
        echo "  ${#paths[@]} file(s) — all present, skipped."
        return
    fi

    # Extract to a temp directory, then move into place.
    local tmpdir
    tmpdir=$(mktemp -d)
    trap "rm -rf '$tmpdir'" RETURN

    if unzip -q -o "$CACHE_ZIP" "${entries[@]}" -d "$tmpdir" 2>/dev/null; then
        for f in "${paths[@]}"; do
            local src="$tmpdir/$ZIP_PREFIX/$f"
            local dst="$OUT_DIR/$f"
            if [[ -f "$src" ]]; then
                mkdir -p "$(dirname "$dst")"
                mv "$src" "$dst"
                extracted=$((extracted + 1))
            fi
        done
    else
        echo "  Warning: unzip reported errors."
    fi

    if [[ $skipped -gt 0 ]]; then
        echo "  $extracted extracted, $skipped already present."
    else
        echo "  $extracted file(s) extracted."
    fi
}

extract_category() {
    local cat="$1"
    echo ""
    echo "[$cat] ${CATEGORIES[$cat]}"
    extract_files "${cat}_files"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if [[ "${1:-}" == "--list" ]]; then
    list_categories
    exit 0
fi

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "$OUT_DIR" "$CACHE_ZIP"
    echo "Cleaned test images and cached archive."
    exit 0
fi

echo "EXRay test image fetcher"
echo "Target: $OUT_DIR"

# Which categories to fetch?
if [[ $# -gt 0 ]]; then
    SELECTED=("$@")
else
    SELECTED=("${ALL_CATEGORIES[@]}")
fi

# Validate category names.
for cat in "${SELECTED[@]}"; do
    if [[ -z "${CATEGORIES[$cat]:-}" ]]; then
        echo "Unknown category: $cat"
        list_categories
        exit 1
    fi
done

# Check if all requested files already exist.
all_present=true
for cat in "${SELECTED[@]}"; do
    declare -n _check="${cat}_files"
    for f in "${_check[@]}"; do
        if [[ ! -f "$OUT_DIR/$f" ]]; then
            all_present=false
            break 2
        fi
    done
    unset -n _check
done

if $all_present; then
    echo "All requested images already present. Nothing to do."
    exit 0
fi

ensure_zip

total=0
for cat in "${SELECTED[@]}"; do
    extract_category "$cat"
    declare -n _arr="${cat}_files"
    total=$((total + ${#_arr[@]}))
    unset -n _arr
done

echo ""
echo "Done. $total file(s) across ${#SELECTED[@]} category(s)."
echo "Images are in: $OUT_DIR"
echo ""
echo "Tip: run with --clean to remove images and cached archive."
