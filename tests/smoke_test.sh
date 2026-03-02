#!/usr/bin/env bash
# smoke_test.sh — GUI integration test for EXRay.
# Launches the app with --smoke-test for each test image.
# In this mode the app forces WARP, suppresses dialogs,
# renders one frame, and exits with 0 (pass) or 1 (fail).
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

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

red()   { printf '\033[31m%s\033[0m' "$1"; }
green() { printf '\033[32m%s\033[0m' "$1"; }
gray()  { printf '\033[90m%s\033[0m' "$1"; }

# Directories where the file must load successfully (exit 0).
# Others (Damaged, MultiView, Beachball, v2, etc.) just must not hang/crash.
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
    local relpath
    relpath=$(python3 -c "import os,sys; print(os.path.relpath(sys.argv[1], sys.argv[2]))" "$file" "$REPO_ROOT" 2>/dev/null || echo "$file")

    local exit_code=0
    "$EXRAY" --smoke-test "$file" >/dev/null 2>&1 || exit_code=$?

    if expects_load "$file"; then
        # MustLoad: exit 0 = pass, anything else = fail
        if [[ $exit_code -eq 0 ]]; then
            printf "  %s  %s\n" "$(green PASS)" "$relpath"
            return 0
        else
            printf "  %s  %s\n" "$(red FAIL)" "$relpath"
            return 1
        fi
    else
        # NoCrash: any clean exit is fine (load errors are expected)
        printf "  %s  %s  %s\n" "$(green PASS)" "$relpath" "$(gray "(exit $exit_code, ok)")"
        return 0
    fi
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
        ((passed++)) || true
    else
        ((failed++)) || true
    fi
done

echo ""
if [[ $failed -eq 0 ]]; then
    echo "$(green "All $passed test(s) passed.")"
else
    echo "$(red "$failed of $((passed + failed)) test(s) FAILED.")"
fi

exit $((failed > 0 ? 1 : 0))
