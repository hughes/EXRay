#!/usr/bin/env bash
# run_tests.sh — Orchestrator for EXRay automated tests.
#
# Usage:
#   ./tests/run_tests.sh                # headless --validate (all categories)
#   ./tests/run_tests.sh --gui          # GUI smoke test (requires display)
#   ./tests/run_tests.sh --all          # both
#   ./tests/run_tests.sh edge damaged   # validate specific categories only

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
EXRAY="$REPO_ROOT/bazel-bin/EXRay.exe"
IMAGES_DIR="$SCRIPT_DIR/images"

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------

MODE="validate"  # validate | gui | all
CATEGORIES=()

for arg in "$@"; do
    case "$arg" in
        --gui) MODE="gui" ;;
        --benchmark) MODE="benchmark" ;;
        --all) MODE="all" ;;
        --help|-h)
            echo "Usage: $0 [--gui|--all] [category...]"
            echo ""
            echo "Modes:"
            echo "  (default)   Headless --validate mode (CI-friendly)"
            echo "  --gui       GUI smoke test (requires display)"
            echo "  --benchmark Performance benchmark (JSON output)"
            echo "  --all       Both validate and GUI"
            echo ""
            echo "Categories: scanline tiled lumachroma edge displaywindow multipart multiview deep damaged"
            echo "If no categories specified, all downloaded images are tested."
            exit 0
            ;;
        *) CATEGORIES+=("$arg") ;;
    esac
done

# ---------------------------------------------------------------------------
# Step 1: Fetch images if needed
# ---------------------------------------------------------------------------

if [[ ! -d "$IMAGES_DIR" ]] || [[ -z "$(ls -A "$IMAGES_DIR" 2>/dev/null)" ]]; then
    echo "Test images not found. Downloading..."
    if [[ ${#CATEGORIES[@]} -gt 0 ]]; then
        bash "$SCRIPT_DIR/fetch_test_images.sh" "${CATEGORIES[@]}"
    else
        bash "$SCRIPT_DIR/fetch_test_images.sh"
    fi
    echo ""
elif [[ ${#CATEGORIES[@]} -gt 0 ]]; then
    # Ensure requested categories are fetched.
    bash "$SCRIPT_DIR/fetch_test_images.sh" "${CATEGORIES[@]}"
    echo ""
fi

# ---------------------------------------------------------------------------
# Step 2: Build EXRay
# ---------------------------------------------------------------------------

if [[ ! -x "$EXRAY" ]]; then
    echo "Building EXRay..."
    export BAZEL_SH="${BAZEL_SH:-C:/cygwin64/bin/bash.exe}"
    (cd "$REPO_ROOT" && bazelisk build //:EXRay)
    echo ""
fi

# ---------------------------------------------------------------------------
# Step 3: Run tests
# ---------------------------------------------------------------------------

result=0

if [[ "$MODE" == "validate" || "$MODE" == "all" ]]; then
    echo "=== Headless validation ==="
    RESULTS_FILE="$SCRIPT_DIR/validate_results.txt"
    # --validate writes results to a file (GUI subsystem binary can't write to pty).
    "$EXRAY" --validate "$IMAGES_DIR" "$RESULTS_FILE" || result=1
    if [[ -f "$RESULTS_FILE" ]]; then
        cat "$RESULTS_FILE"
        rm -f "$RESULTS_FILE"
    fi
    echo ""
fi

if [[ "$MODE" == "gui" || "$MODE" == "all" ]]; then
    echo "=== GUI smoke test ==="
    bash "$SCRIPT_DIR/smoke_test.sh" "$IMAGES_DIR" || result=1
    echo ""
fi

if [[ "$MODE" == "benchmark" ]]; then
    echo "=== Performance benchmark ==="
    BENCH_FILE="$SCRIPT_DIR/benchmark_results.json"
    "$EXRAY" --benchmark "$IMAGES_DIR" "$BENCH_FILE"
    if [[ -f "$BENCH_FILE" ]]; then
        cat "$BENCH_FILE"
        rm -f "$BENCH_FILE"
    fi
    echo ""
fi

exit $result
