#!/usr/bin/env bash
# In-editor rendering benchmark for XARA_RENDERER=cairo (Xvfb). Creates a 16-object
# scene through the automation hook (text lines, ellipses, rectangles, lines; zoom to
# drawing), lets the editor render it, and records XARA_CAIRO_STATS: per-operation
# counts and cumulative wall time of Cairo-drawn and GDraw-delegated draw calls.
# Variants are run alternately, so machine-load changes affect them alike.
# Usage: bench_editor_cairo.sh RUNS NAME=/abs/XaraLX [NAME=/abs/XaraLX ...]
set -euo pipefail
if [[ ${1:-} != --inside ]]; then
    (( $# >= 2 )) || { echo "Usage: $0 RUNS NAME=/abs/XaraLX ..." >&2; exit 2; }
    run_dir=$(mktemp -d "${TMPDIR:-/tmp}/xara-editor-bench.XXXXXX")
    echo "Editor benchmark evidence: $run_dir"
    exec xvfb-run -a -e "$run_dir/xvfb.log" -s '-screen 0 1280x900x24' "$0" --inside "$run_dir" "$@"
fi
run_dir=$2; runs=$3; shift 3
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
ulimit -c 0
hex() { printf '%s' "$1" | od -An -tx1 | tr -d ' \n'; }
text='THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789 vengeance'
{
    printf '<?xml version='"'"'1.0'"'"' encoding='"'"'utf-8'"'"'?>\n<requests transaction="true">'
    for i in 0 1 2 3 4 5 6 7; do
        printf '<request method="object.create_text" x="36000" y="%d" font_size="%d" fill="#202020" stroke="none" text_hex="%s" />' \
            $((560000 - i * 52000)) $((18000 + i * 2000)) "$(hex "$text")"
    done
    for i in 0 1 2; do
        printf '<request method="object.create_ellipse" x="%d" y="600000" width="90000" height="70000" fill="#D4AF37" stroke="none" />' $((40000 + i * 110000))
    done
    printf '<request method="object.create_rectangle" x="380000" y="600000" width="120000" height="80000" fill="#185ADB" stroke="none" />'
    printf '<request method="object.create_rectangle" x="520000" y="600000" width="60000" height="140000" fill="#2E7D32" stroke="none" />'
    printf '<request method="object.create_line" x="36000" y="20000" x2="560000" y2="120000" stroke="#E53935" stroke_width="3000" />'
    printf '<request method="object.create_line" x="36000" y="120000" x2="560000" y2="20000" stroke="#1E88E5" stroke_width="1500" />'
    printf '<request method="object.create_line" x="300000" y="10000" x2="300000" y2="740000" stroke="#000000" stroke_width="750" />'
    printf '</requests>'
} > "$run_dir/request.xml"
echo "variant,run,operation,cairo,gdraw,cairo_us,gdraw_us" > "$run_dir/results.csv"
for ((r = 1; r <= runs; r++)); do
    for spec in "$@"; do
        name=${spec%%=*}; binary=${spec#*=}
        case_dir="$run_dir/$name-$r"; mkdir -p "$case_dir/settings"; chmod 700 "$case_dir/settings"
        cp "$run_dir/request.xml" "$case_dir/request.xml"; chmod 600 "$case_dir/request.xml"
        (cd -- "$(dirname -- "$binary")" && exec env XARA_RENDERER=cairo XARA_CAIRO_STATS="$case_dir/stats.txt" \
            XARA_CONFIG_DIR="$case_dir/settings" XARA_DATA_DIR="$repo_dir" \
            XARA_AUTOMATION_POC_REQUEST="$case_dir/request.xml" XARA_AUTOMATION_POC_RESPONSE="$case_dir/response.xml" \
            XARA_AUTOMATION_POC_FIT_DRAWING=1 "$binary") > "$case_dir/stdout.log" 2> "$case_dir/stderr.log" &
        pid=$!
        for ((t = 0; t < 60; t++)); do sleep 1; [[ -s $case_dir/response.xml ]] && break; kill -0 $pid 2>/dev/null || break; done
        sleep 10
        kill -TERM $pid 2>/dev/null || true; sleep 2; kill -KILL $pid 2>/dev/null || true; wait $pid 2>/dev/null || true
        if grep -q 'error=' "$case_dir/response.xml" 2>/dev/null || [[ ! -s $case_dir/stats.txt ]]; then
            echo "FAIL: $name run $r (no response/statistics)"; exit 1
        fi
        tail -n +2 "$case_dir/stats.txt" | awk -v n="$name" -v r="$r" '{print n","r","$1","$2","$3","$4","$5}' >> "$run_dir/results.csv"
        echo "$name run $r: $(awk '{c+=$4; g+=$5} END {printf "cairo %.1f ms, gdraw %.1f ms", c/1000, g/1000}' <(tail -n +2 "$case_dir/stats.txt"))"
    done
done
echo "Results: $run_dir/results.csv"
