#!/usr/bin/env bash
# compare_benchmark.sh — Compare benchmark results against a baseline.
#
# Usage:
#   ./tests/compare_benchmark.sh baseline.json current.json
#
# Prints per-file timing comparison with delta percentages.
# Regressions (>10% slower) highlighted in red, improvements in green.
# Exit code is always 0 (informational only).

set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <baseline.json> <current.json>"
    exit 1
fi

BASELINE="$1"
CURRENT="$2"

python3 - "$BASELINE" "$CURRENT" << 'PYEOF'
import json, sys

def load(path):
    with open(path) as f:
        return json.load(f)

baseline = load(sys.argv[1])
current = load(sys.argv[2])

base_by_path = {f["path"]: f for f in baseline["files"]}

header = f"{'File':<50} {'Base Load':>10} {'Curr Load':>10} {'Delta':>8}  {'Base Hist':>10} {'Curr Hist':>10} {'Delta':>8}"
print(header)
print("-" * len(header))

for f in current["files"]:
    path = f["path"]
    if "error" in f:
        continue
    b = base_by_path.get(path)
    if not b or "error" in b:
        print(f"{path:<50} {'(new)':>10} {f['median_load_ms']:>9.1f}ms {'':>8}  {'(new)':>10} {f['median_hist_ms']:>9.1f}ms")
        continue

    load_delta = (f["median_load_ms"] - b["median_load_ms"]) / b["median_load_ms"] * 100 if b["median_load_ms"] > 0 else 0
    hist_delta = (f["median_hist_ms"] - b["median_hist_ms"]) / b["median_hist_ms"] * 100 if b["median_hist_ms"] > 0 else 0

    def fmt_delta(d):
        sign = "+" if d > 0 else ""
        color = "\033[31m" if d > 10 else "\033[32m" if d < -10 else ""
        reset = "\033[0m" if color else ""
        return f"{color}{sign}{d:.1f}%{reset}"

    print(f"{path:<50} {b['median_load_ms']:>9.1f}ms {f['median_load_ms']:>9.1f}ms {fmt_delta(load_delta):>16}  "
          f"{b['median_hist_ms']:>9.1f}ms {f['median_hist_ms']:>9.1f}ms {fmt_delta(hist_delta):>16}")

bs = baseline["summary"]
cs = current["summary"]
print()
print(f"Aggregate load throughput:  {bs['load_mpx_per_sec']:.1f} -> {cs['load_mpx_per_sec']:.1f} MPx/s")
print(f"Aggregate hist throughput:  {bs['hist_mpx_per_sec']:.1f} -> {cs['hist_mpx_per_sec']:.1f} MPx/s")
PYEOF
