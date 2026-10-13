#!/usr/bin/env bash
# Build and run the CDraw vs Cairo-core differential test and the unsupported-operation
# (fallback) test. No application build is needed.
# Usage: tests/native/run_cairo_differential.sh [-v] [-s scene-substring] [results.csv]
#        tests/native/run_cairo_differential.sh --fallback     (fallback test only)
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
out_dir=$(mktemp -d "${TMPDIR:-/tmp}/xara-cairo-diff.XXXXXX")
build() {
    "${CXX:-g++}" -std=c++17 -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
        -I "$repo_dir/tests/native" -I "$repo_dir/GDraw" -I "$repo_dir/Kernel" $(pkg-config --cflags cairo) \
        "$repo_dir/tests/native/$1.cpp" "$repo_dir/Kernel/cairocore.cpp" \
        -L "$repo_dir/libs/x86_64" -lCDraw $(pkg-config --libs cairo) -no-pie -o "$out_dir/$1"
}
build check_cairo_fallback
if [[ ${1:-} == --fallback ]]; then
    exec "$out_dir/check_cairo_fallback"
fi
build check_cairo_differential
status=0
"$out_dir/check_cairo_differential" "$@" || status=1
if [[ $# == 0 || ! " $* " =~ " -s " ]]; then
    "$out_dir/check_cairo_fallback" || status=1
fi
exit $status
