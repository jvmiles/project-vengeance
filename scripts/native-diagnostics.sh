#!/usr/bin/env bash
# Build products stay outside the source tree. This does not install anything.
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/xara-native.XXXXXX")
printf 'Diagnostic build directory: %s\n' "$build_dir"
if [[ $(uname -m) != x86_64 ]]; then
    echo 'CDraw probe requires x86_64.' >&2
    exit 1
fi
"${CC:-gcc}" -Wall -Wextra -c "$source_dir/wxOil/binreloc.c" -o "$build_dir/binreloc.o"
"${CXX:-g++}" -std=c++11 -no-pie -Wall -Wextra -I"$source_dir/GDraw" \
    "$source_dir/tests/native/cdraw_probe.cpp" \
    "$source_dir/libs/x86_64/libCDraw.a" -lm -o "$build_dir/cdraw-probe"
"$build_dir/cdraw-probe"
echo 'PASS: binreloc compiles; CDraw links and initializes. Rendering remains untested.'
missing=0
for tool in autoreconf autoconf automake aclocal wx-config xml2-config pkg-config; do
    if ! command -v "$tool" >/dev/null; then
        printf 'Missing application build tool: %s\n' "$tool"
        missing=1
    fi
done
if [[ $missing == 1 ]]; then exit 2; fi
echo 'Application tools found; this probe does not configure or build XaraLX.'
