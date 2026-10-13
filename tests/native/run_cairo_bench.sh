#!/usr/bin/env bash
# Build and run the CDraw vs Cairo-core benchmark pinned to one CPU.
# Usage: tests/native/run_cairo_bench.sh [--baseline <git-commit>] [results.csv]
#   --baseline: also compile that commit's (or directory's) Kernel/cairocore.* into
#               namespace XaraCairoBase and measure it interleaved in the same runs
#               (paired before/after).
#   XARA_CAIRO_PROFILE=1: add per-phase timings of the current core.
#   XARA_BENCH_CPU=<n>: CPU to pin to (default 3).
#   Compiled with -O3 -fno-strict-aliasing, as the Xara Kernel library is.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out_dir=$(mktemp -d "${TMPDIR:-/tmp}/xara-cairo-bench.XXXXXX")
extra=()
sources=("$repo_dir/tests/native/bench_cairo_core.cpp" "$repo_dir/Kernel/cairocore.cpp")
[[ -n ${XARA_CAIRO_PROFILE:-} ]] && extra+=(-DXARA_CAIRO_PROFILE)
if [[ ${1:-} == --baseline ]]; then
    mkdir -p "$out_dir/baseline"
    for f in cairocore.h cairocore.cpp; do
        { if [[ -d $2 ]]; then cat "$2/$f"; else git -C "$repo_dir" show "$2:Kernel/$f"; fi; } |
            sed -e 's/^namespace XaraCairo$/namespace XaraCairoBase/' -e 's/^namespace XaraCairo {/namespace XaraCairoBase {/' \
                -e 's/XARA_CAIRO_CORE_H/XARA_CAIRO_CORE_BASE_H/g' -e 's|#include "cairocore.h"|#include "baseline/cairocore.h"|' \
                -e 's/XARA_CAIRO_PROFILE/XARA_CAIRO_PROFILE_BASELINE_OFF/g' > "$out_dir/baseline/$f"
    done
    extra+=(-DXARA_CAIRO_BASELINE -I "$out_dir")
    sources+=("$out_dir/baseline/cairocore.cpp")
    shift 2
fi
"${CXX:-g++}" -std=c++17 -O3 -fno-strict-aliasing -Wall -Wextra -Wno-unused-parameter "${extra[@]}" \
    -I "$repo_dir/tests/native" -I "$repo_dir/GDraw" -I "$repo_dir/Kernel" $(pkg-config --cflags cairo) \
    "${sources[@]}" -L "$repo_dir/libs/x86_64" -lCDraw $(pkg-config --libs cairo) -no-pie -o "$out_dir/bench_cairo_core"
exec taskset -c "${XARA_BENCH_CPU:-3}" "$out_dir/bench_cairo_core" "$@"
