#!/usr/bin/env bash
# smoke_test.sh — GUI integration test for EXRay.
# Launches the real app with each test image, verifies the window appears
# and the process doesn't crash, then closes it.
#
# Requires a display (not for headless CI).
#
# Usage:
#   ./tests/smoke_test.sh                        # test all downloaded images
#   ./tests/smoke_test.sh tests/images/ScanLines  # test one category
#   ./tests/smoke_test.sh file.exr               # test a single file

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EXRAY="$REPO_ROOT/bazel-bin/EXRay.exe"
IMAGES_DIR="$SCRIPT_DIR/images"
TIMEOUT_SEC=8

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

red()   { printf '\033[31m%s\033[0m' "$1"; }
green() { printf '\033[32m%s\033[0m' "$1"; }

# Directories where we expect the file to load successfully (window title changes).
expects_load() {
    case "$1" in
        */ScanLines/*|*/Tiles/*|*/TestImages/*|*/LuminanceChroma/*|*/DisplayWindow/*|*/Chromaticities/*)
            return 0 ;;
        *)
            return 1 ;;
    esac
}

smoke_one() {
    local file="$1"
    local basename
    basename=$(basename "$file")
    local relpath
    relpath=$(python3 -c "import os,sys; print(os.path.relpath(sys.argv[1], sys.argv[2]))" "$file" "$REPO_ROOT" 2>/dev/null || echo "$file")

    # Launch EXRay in background.
    "$EXRAY" "$file" &
    local pid=$!

    # Wait for the window to appear (poll for up to TIMEOUT_SEC seconds).
    local found=false
    for (( i=0; i<TIMEOUT_SEC*2; i++ )); do
        if ! kill -0 "$pid" 2>/dev/null; then
            break  # Process exited
        fi
        # Check if the window exists via PowerShell.
        local title
        title=$(powershell.exe -NoProfile -Command "
            \$p = Get-Process -Id $pid -ErrorAction SilentlyContinue
            if (\$p -and \$p.MainWindowTitle) { Write-Output \$p.MainWindowTitle }
        " 2>/dev/null | tr -d '\r') || true
        if [[ -n "$title" ]]; then
            found=true
            break
        fi
        sleep 0.5
    done

    # Evaluate result.
    local crashed=false
    if ! kill -0 "$pid" 2>/dev/null; then
        wait "$pid" 2>/dev/null || true
        crashed=true
    fi

    local passed=true
    local detail=""

    if $crashed; then
        passed=false
        detail="crashed"
    elif ! $found; then
        passed=false
        detail="no window after ${TIMEOUT_SEC}s"
    else
        # Check window title for filename (success indicator).
        if expects_load "$file"; then
            if [[ "$title" == *"$basename"* ]]; then
                detail="title ok"
            else
                passed=false
                detail="title='$title' (expected filename)"
            fi
        else
            detail="window appeared"
        fi
    fi

    # Clean up: close the window gracefully.
    if kill -0 "$pid" 2>/dev/null; then
        # Send WM_CLOSE via PowerShell.
        powershell.exe -NoProfile -Command "
            \$p = Get-Process -Id $pid -ErrorAction SilentlyContinue
            if (\$p) { \$p.CloseMainWindow() | Out-Null }
        " 2>/dev/null || true

        # Wait briefly for graceful shutdown, then force-kill if needed.
        for (( i=0; i<6; i++ )); do
            if ! kill -0 "$pid" 2>/dev/null; then break; fi
            sleep 0.5
        done
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
        fi
    fi
    wait "$pid" 2>/dev/null || true

    if $passed; then
        printf "  %s  %-50s  %s\n" "$(green PASS)" "$relpath" "$detail"
    else
        printf "  %s  %-50s  %s\n" "$(red FAIL)" "$relpath" "$detail"
    fi

    $passed
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if [[ ! -x "$EXRAY" ]]; then
    echo "EXRay binary not found at $EXRAY"
    echo "Build first: export BAZEL_SH=C:/cygwin64/bin/bash.exe && bazelisk build //:EXRay"
    exit 1
fi

# Collect files.
target="${1:-$IMAGES_DIR}"

files=()
if [[ -d "$target" ]]; then
    while IFS= read -r f; do
        files+=("$f")
    done < <(find "$target" -type f \( -name '*.exr' -o -name '*.EXR' \) | sort)
elif [[ -f "$target" ]]; then
    files=("$target")
else
    echo "Error: '$target' is not a file or directory."
    exit 1
fi

if [[ ${#files[@]} -eq 0 ]]; then
    echo "No EXR files found in '$target'."
    exit 1
fi

echo ""
echo "EXRay GUI smoke test: ${#files[@]} file(s)"
echo ""

passed=0
failed=0
for f in "${files[@]}"; do
    if smoke_one "$f"; then
        ((passed++))
    else
        ((failed++))
    fi
done

echo ""
if [[ $failed -eq 0 ]]; then
    echo "$(green "All $passed test(s) passed.")"
else
    echo "$(red "$failed of $((passed + failed)) test(s) FAILED.")"
fi

exit $((failed > 0 ? 1 : 0))
