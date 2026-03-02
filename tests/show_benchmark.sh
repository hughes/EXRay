#!/usr/bin/env bash
# show_benchmark.sh — Pretty-print benchmark results as a human-readable table.
#
# Usage:
#   ./tests/show_benchmark.sh [benchmark_results.json]
#
# If no file is given, runs the benchmark first.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if [[ $# -ge 1 ]]; then
    INPUT="$1"
else
    EXRAY="$REPO_ROOT/bazel-bin/EXRay.exe"
    if [[ ! -x "$EXRAY" ]]; then
        echo "EXRay.exe not found. Build first." >&2
        exit 1
    fi
    INPUT="$SCRIPT_DIR/benchmark_results.json"
    "$EXRAY" --benchmark "$SCRIPT_DIR/images" "$INPUT" 2>/dev/null
fi

python3 - "$INPUT" << 'PYEOF'
import json, sys

with open(sys.argv[1]) as f:
    data = json.load(f)

files = [f for f in data["files"] if "error" not in f]
summary = data["summary"]

# Sort by median load time descending (slowest first)
files.sort(key=lambda f: f["median_load_ms"], reverse=True)

max_load = max(f["load_mpx_per_sec"] for f in files)
max_hist = max(f["hist_mpx_per_sec"] for f in files)
bar_width = 20

def bar(val, mx):
    filled = round(val / mx * bar_width) if mx > 0 else 0
    return "#" * filled + "." * (bar_width - filled)

# Header
print(f"\n  EXRay Benchmark  ({data['iterations']} iterations, median)")
print(f"  {len(files)} files, {summary['total_megapixels']:.1f} total MPx\n")

print(f"  {'File':<40} {'Size':>10} {'Load':>8} {'Hist':>8}  {'Load throughput':<24} {'Hist throughput':<24}")
print(f"  {'':-<40} {'':-<10} {'':-<8} {'':-<8}  {'':-<24} {'':-<24}")

for f in files:
    name = f["path"].replace("\\", "/")
    # Shorten long paths
    if len(name) > 38:
        name = "..." + name[-(38-3):]
    size = f"{f['width']}x{f['height']}"
    load = f"{f['median_load_ms']:.1f}ms"
    hist = f"{f['median_hist_ms']:.1f}ms"
    load_bar = bar(f["load_mpx_per_sec"], max_load)
    hist_bar = bar(f["hist_mpx_per_sec"], max_hist)
    load_tp = f"{f['load_mpx_per_sec']:.1f}"
    hist_tp = f"{f['hist_mpx_per_sec']:.1f}"

    print(f"  {name:<40} {size:>10} {load:>8} {hist:>8}  {load_bar} {load_tp:>5}  {hist_bar} {hist_tp:>5}")

# Summary
print(f"\n  {'Aggregate':<40} {'':>10} {summary['total_load_ms']:.0f}ms {summary['total_hist_ms']:.0f}ms")
print(f"  {'Throughput (MPx/s)':<40} {'':>10} {summary['load_mpx_per_sec']:>7.1f} {summary['hist_mpx_per_sec']:>7.1f}")
print()
PYEOF
