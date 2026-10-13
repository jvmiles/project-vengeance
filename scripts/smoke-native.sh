#!/usr/bin/env bash
# Isolated startup evidence only: does not establish editing/export correctness.
set -euo pipefail
if [[ ${1:-} != --inside ]]; then
    if [[ $# != 1 ]]; then echo "Usage: $0 /absolute/path/to/XaraLX" >&2; exit 2; fi
    executable=$(realpath -- "$1")
    [[ -x $executable ]] || { echo "Not executable: $executable" >&2; exit 2; }
    for tool in xvfb-run Xvfb xauth xdpyinfo xwininfo xprop timeout; do
        command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 2; }
    done
    run_dir=$(mktemp -d "${TMPDIR:-/tmp}/xara-smoke.XXXXXX")
    mkdir "$run_dir/settings"
    echo "Startup evidence: $run_dir"
    ldd "$executable" > "$run_dir/libraries.log" 2>&1
    if grep -q 'not found' "$run_dir/libraries.log"; then cat "$run_dir/libraries.log"; exit 1; fi
    exec xvfb-run -a -e "$run_dir/xvfb.log" -s '-screen 0 1280x900x24' \
        "$0" --inside "$executable" "$run_dir"
fi
executable=$2
run_dir=$3
export XARA_CONFIG_DIR="$run_dir/settings"
export XARA_ASSERT_TRACE=1
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export XARA_DATA_DIR=$(cd -- "$script_dir/.." && pwd)
# Disable core dumps; startup failures are captured in logs/status.
ulimit -c 0
xdpyinfo > "$run_dir/display.log" 2>&1 || { echo 'Xvfb display is inaccessible.'; exit 1; }
cd -- "$(dirname -- "$executable")"
set +e
timeout 20s "$executable" --version > "$run_dir/version.log" 2>&1
version_status=$?
set -e
printf 'Version command exit: %s (historical OnInit returns false for --version)\n' "$version_status" > "$run_dir/status.log"
"$executable" > "$run_dir/startup.log" 2>&1 &
app_pid=$!
cleanup() {
    if kill -0 "$app_pid" 2>/dev/null; then
        kill -TERM "$app_pid" 2>/dev/null || true
        sleep 1
        kill -KILL "$app_pid" 2>/dev/null || true
    fi
    wait "$app_pid" 2>/dev/null || true
}
trap cleanup EXIT
for ((attempt=0; attempt<20; attempt++)); do
    if ! kill -0 "$app_pid" 2>/dev/null; then
        set +e
        wait "$app_pid"
        app_status=$?
        set -e
        echo "Application exited before observation: $app_status" | tee -a "$run_dir/status.log"
        cat "$run_dir/startup.log"
        exit 1
    fi
    sleep 1
done
xwininfo -root -tree > "$run_dir/windows.log" 2>&1
if command -v import >/dev/null; then
    import -window root "$run_dir/startup.png" 2> "$run_dir/screenshot.log" || true
fi
# A matching title is evidence, not proof of a functional editor.
if grep -qE 'assert .*failed|XARA_ASSERT_BACKTRACE_BEGIN' "$run_dir/startup.log"; then
    echo 'Startup assertion detected; any editor window may be blocked by a dialog.' | tee -a "$run_dir/status.log"
    cat "$run_dir/startup.log"
    exit 1
fi
if grep -q 'Xara Xtreme' "$run_dir/windows.log"; then
    echo 'Candidate Xara window found; inspect windows.log and startup.png to confirm the editor.' | tee -a "$run_dir/status.log"
    echo 'Process survived 20 seconds. Editing, XAR round trips and exports remain untested.' | tee -a "$run_dir/status.log"
else
    echo 'No Xara Xtreme window found.' | tee -a "$run_dir/status.log"
    cat "$run_dir/startup.log"
    exit 1
fi
