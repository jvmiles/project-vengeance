#!/usr/bin/env bash
# Runtime check of XARA_RENDERER selection in the real application under Xvfb.
# Scenarios: "blank" (default new document) and "scene" (rectangle, ellipse, line
# and text created through the automation hook, then zoom-to-drawing). For each
# renderer setting the editor frame must appear and no assertion may fire.
#  - unset/gdraw: no renderer diagnostics; pixel-identical to the default run.
#  - unknown value: exactly one fallback warning; pixel-identical to the default run.
#  - cairo: exactly one activation notice and no fallback warning; no target bitmap may
#    be rejected; Cairo must actually draw (XARA_CAIRO_STATS): view rectangles, and in
#    the scene the document objects' fills and stroke, which render into transparency-
#    channel (0x80000001) captures; compositing a capture into the view (a bitmap fill)
#    must be reported as drawn by gdraw;
#    no screen pixel may differ from the default run by more than 51 levels (one CDraw
#    antialias sub-scanline, as in tests/native/check_cairo_differential.cpp).
# With a reference executable (e.g. the pre-factory build), its default rendering must
# be pixel-identical to the default run too.
# Note: .xar files passed on the command line are not loaded by the
# --disable-filters build, so documents are created through automation instead.
# Usage: check_renderer_runtime.sh /abs/XaraLX [/abs/reference/XaraLX]
set -euo pipefail
if [[ ${1:-} != --inside ]]; then
    if [[ $# -lt 1 || $# -gt 2 ]]; then echo "Usage: $0 XaraLX [reference-XaraLX]" >&2; exit 2; fi
    for tool in xvfb-run Xvfb xauth xdpyinfo xwininfo import compare timeout; do
        command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 2; }
    done
    executable=$(realpath -- "$1")
    reference=${2:+$(realpath -- "$2")}
    [[ -x $executable ]] || { echo "Not executable: $executable" >&2; exit 2; }
    [[ -z $reference || -x $reference ]] || { echo "Not executable: $reference" >&2; exit 2; }
    run_dir=$(mktemp -d "${TMPDIR:-/tmp}/xara-renderer-runtime.XXXXXX")
    echo "Renderer runtime evidence: $run_dir"
    exec xvfb-run -a -e "$run_dir/xvfb.log" -s '-screen 0 1280x900x24' \
        "$0" --inside "$run_dir" "$executable" "$reference"
fi
run_dir=$2
executable=$3
reference=${4:-}
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# GTK scrollbars update asynchronously after zoom-to-drawing; 6s once caught one
# mid-update (6200 pixels, scrollbar thumbs only). 12s has been stable.
settle=${XARA_RUNTIME_SETTLE:-12}
failures=0
ulimit -c 0
xdpyinfo > "$run_dir/display.log" 2>&1 || { echo 'Xvfb display is inaccessible.'; exit 1; }

# tests/native/fixtures/automation-scene.json, encoded by scripts/automation-poc.py.
scene_xml='<?xml version='"'"'1.0'"'"' encoding='"'"'utf-8'"'"'?>
<requests transaction="true"><request method="document.list_objects" /><request method="object.create_rectangle" x="72000" y="180000" width="180000" height="108000" fill="#185ADB" stroke="none" /><request method="object.create_ellipse" x="288000" y="180000" width="108000" height="108000" fill="#D4AF37" stroke="none" /><request method="object.create_line" x="72000" y="144000" x2="396000" y2="108000" stroke="#E53935" stroke_width="3000" /><request method="object.create_text" x="72000" y="54000" font_size="24000" fill="#202020" stroke="none" text_hex="50524f4a4543542056454e4745414e4345" /><request method="document.list_objects" /></requests>'

fail() { echo "FAIL: $*" | tee -a "$run_dir/results.log"; failures=$((failures + 1)); }
pass() { echo "PASS: $*" | tee -a "$run_dir/results.log"; }

# run_case NAME BINARY SCENARIO(blank|scene) XARA_RENDERER-value|"<unset>"
run_case() {
    local name=$1 binary=$2 scenario=$3 renderer=$4
    local case_dir="$run_dir/$name"
    mkdir -p "$case_dir/settings"
    chmod 700 "$case_dir/settings"
    local -a env_args=(XARA_CONFIG_DIR="$case_dir/settings" XARA_DATA_DIR="$repo_dir" XARA_ASSERT_TRACE=1
                       XARA_CAIRO_STATS="$case_dir/cairo-stats.txt")
    if [[ $scenario == scene ]]; then
        (umask 077; printf '%s' "$scene_xml" > "$case_dir/request.xml")
        env_args+=(XARA_AUTOMATION_POC_REQUEST="$case_dir/request.xml"
                   XARA_AUTOMATION_POC_RESPONSE="$case_dir/response.xml"
                   XARA_AUTOMATION_POC_FIT_DRAWING=1)
    fi
    [[ $renderer == '<unset>' ]] || env_args+=(XARA_RENDERER="$renderer")
    (cd -- "$(dirname -- "$binary")" && exec env -u XARA_RENDERER "${env_args[@]}" "$binary") \
        > "$case_dir/stdout.log" 2> "$case_dir/stderr.log" &
    local pid=$! ready=false
    for ((t = 0; t < 60; t++)); do
        sleep 1
        kill -0 "$pid" 2>/dev/null || break
        xwininfo -root -tree 2>/dev/null | grep -q '"Xara Xtreme' || continue
        [[ $scenario == blank || -s $case_dir/response.xml ]] || continue
        ready=true; break
    done
    if $ready; then
        sleep "$settle"
        xwininfo -root -tree > "$case_dir/windows.log" 2>&1 || true
        import -window root "$case_dir/screen.png" 2> "$case_dir/screenshot.log" || true
    fi
    if kill -0 "$pid" 2>/dev/null; then
        kill -TERM "$pid" 2>/dev/null || true; sleep 2; kill -KILL "$pid" 2>/dev/null || true
    fi
    wait "$pid" 2>/dev/null || true
    $ready || { fail "$name: editor frame${scenario/blank/}${scenario/scene/ or automation response} not ready"; return; }
    [[ -s $case_dir/screen.png ]] || { fail "$name: no screenshot"; return; }
    if grep -qE 'XARA_(ASSERT|FATAL)_BACKTRACE_BEGIN|assert .*failed' "$case_dir/stdout.log" "$case_dir/stderr.log"; then
        fail "$name: assertion"; return
    fi
    if [[ $scenario == scene ]] && grep -q 'error=' "$case_dir/response.xml"; then
        fail "$name: automation rejected the scene"; return
    fi
    pass "$name: editor ready and screenshot captured"
}

# expect_diagnostics NAME EXPECTED_COUNT REQUIRED_TEXT...
expect_diagnostics() {
    local name=$1 want=$2; shift 2
    local log="$run_dir/$name/stderr.log" count text
    [[ -s $log ]] || { fail "$name: empty stderr (startup not reached?)"; return; }
    count=$(grep -c '^Xara renderer' "$log" || true)
    if [[ $count != "$want" ]]; then fail "$name: $count renderer diagnostic line(s), expected $want"; return; fi
    for text in "$@"; do
        grep -qF -- "$text" "$log" || { fail "$name: diagnostic lacks '$text'"; return; }
    done
    pass "$name: $want renderer diagnostic line(s)"
}

# same_pixels NAME_A NAME_B
same_pixels() {
    local a="$run_dir/$1/screen.png" b="$run_dir/$2/screen.png" diff
    [[ -s $a && -s $b ]] || { fail "$1 vs $2: missing screenshot"; return; }
    diff=$(compare -metric AE "$a" "$b" "$run_dir/$1--$2.diff.png" 2>&1 >/dev/null || true)
    if [[ $diff == 0 ]]; then pass "$1 vs $2: identical pixels"
    else fail "$1 vs $2: $diff differing pixels"; fi
}

# expect_cairo NAME: one activation notice, no renderer fallback; record delegations.
expect_cairo() {
    local name=$1 log="$run_dir/$1/stderr.log" count
    [[ -s $log ]] || { fail "$name: empty stderr (startup not reached?)"; return; }
    count=$(grep -c '^Xara renderer: using cairo' "$log" || true)
    [[ $count == 1 ]] || { fail "$name: $count cairo activation notices, expected 1"; return; }
    if grep -qE 'not available|unknown XARA_RENDERER' "$log"; then fail "$name: unexpected renderer fallback"; return; fi
    grep '^Xara renderer: cairo does not draw' "$log" | sort -u > "$run_dir/$name/delegated.log" || true
    pass "$name: cairo active; $(wc -l < "$run_dir/$name/delegated.log") delegated operation kind(s) reported"
}


# expect_delegated NAME TEXT: the given operation kind was reported as drawn by gdraw.
expect_delegated() {
    local name=$1 text=$2
    if grep -qF -- "$text" "$run_dir/$name/delegated.log"; then pass "$name: delegated to gdraw: $text"
    else fail "$name: expected delegation '$text' not reported"; fi
}
# expect_not_delegated NAME TEXT: no delegation matching TEXT was reported.
expect_not_delegated() {
    local name=$1 text=$2
    if grep -qF -- "$text" "$run_dir/$name/delegated.log"; then fail "$name: unexpected delegation: $(grep -F -- "$text" "$run_dir/$name/delegated.log" | head -1)"
    else pass "$name: no delegation for $text"; fi
}

# cairo_drew NAME OP...: XARA_CAIRO_STATS shows Cairo drew each listed operation.
cairo_drew() {
    local name=$1 stats="$run_dir/$1/cairo-stats.txt" op n; shift
    [[ -s $stats ]] || { fail "$name: no cairo statistics written"; return; }
    for op in "$@"; do
        n=$(awk -v o="$op" '$1 == o { print $2 }' "$stats")
        [[ ${n:-0} -gt 0 ]] || { fail "$name: cairo drew no $op"; return; }
    done
    pass "$name: cairo drew $* ($(tail -n +2 "$stats" | tr '\n' ';'))"
}

# similar_pixels NAME_A NAME_B: no pixel differs by more than 51 levels (20%).
similar_pixels() {
    local a="$run_dir/$1/screen.png" b="$run_dir/$2/screen.png" exact over
    [[ -s $a && -s $b ]] || { fail "$1 vs $2: missing screenshot"; return; }
    exact=$(compare -metric AE "$a" "$b" "$run_dir/$1--$2.diff.png" 2>&1 >/dev/null || true)
    over=$(compare -metric AE -fuzz 20% "$a" "$b" null: 2>&1 || true)
    if [[ $over == 0 ]]; then pass "$1 vs $2: $exact pixels differ, none by more than 51 levels"
    else fail "$1 vs $2: $over pixels differ by more than 51 levels ($exact differ at all)"; fi
}

for scenario in blank scene; do
    echo "Scenario: $scenario" | tee -a "$run_dir/results.log"
    run_case "$scenario-default" "$executable" "$scenario" '<unset>'
    run_case "$scenario-gdraw" "$executable" "$scenario" gdraw
    run_case "$scenario-cairo" "$executable" "$scenario" cairo
    run_case "$scenario-unknown" "$executable" "$scenario" skia
    expect_diagnostics "$scenario-default" 0
    expect_diagnostics "$scenario-gdraw" 0
    expect_cairo "$scenario-cairo"
    expect_diagnostics "$scenario-unknown" 1 'warning' "'skia'" 'falling back to gdraw'
    same_pixels "$scenario-default" "$scenario-gdraw"
    similar_pixels "$scenario-default" "$scenario-cairo"
    cairo_drew "$scenario-cairo" FillRectangle
    expect_not_delegated "$scenario-cairo" "bitmap formats"
    if [[ $scenario == scene ]]; then
        # Document objects (rectangle, ellipse, line, text) render into transparency-
        # channel captures, which Cairo now draws; only compositing the capture into the
        # view (a bitmap fill) stays with gdraw.
        cairo_drew "$scenario-cairo" FillPath StrokePath
        expect_delegated "$scenario-cairo" "FillPath with bitmap fills"
    fi
    same_pixels "$scenario-default" "$scenario-unknown"
    if [[ -n $reference ]]; then
        run_case "$scenario-reference" "$reference" "$scenario" '<unset>'
        expect_diagnostics "$scenario-reference" 0
        same_pixels "$scenario-reference" "$scenario-default"
    fi
done

if ((failures)); then echo "$failures failure(s); evidence in $run_dir"; exit 1; fi
echo "PASS: renderer runtime checks; evidence in $run_dir"
