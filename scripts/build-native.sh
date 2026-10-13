#!/usr/bin/env bash
# Build in a disposable source copy; never install or alter system packages.
set -euo pipefail
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
run_dir=$(mktemp -d "${TMPDIR:-/tmp}/xara-build.XXXXXX")
printf 'Build and diagnostic logs: %s\n' "$run_dir"
mkdir "$run_dir/source" "$run_dir/logs"
tar -C "$source_dir" --exclude=.git --exclude=.agents --exclude=.codex \
    --exclude=.aws -cf - . | tar -C "$run_dir/source" -xf -
cd "$run_dir/source"
run_stage() {
    local stage=$1
    shift
    printf 'Stage: %s\n' "$stage"
    "$@" 2>&1 | tee "$run_dir/logs/$stage.log"
}
{
    uname -m
    "${CXX:-g++}" --version
    for tool in autoreconf automake wx-config xml2-config pkg-config; do
        if command -v "$tool" >/dev/null; then
            "$tool" --version
        else
            printf 'Missing tool: %s\n' "$tool"
        fi
    done
} > "$run_dir/logs/toolchain.log" 2>&1
# Localization-disabled bootstrap can use gettext files already installed.
# This avoids installing autopoint; full localization still requires autopoint.
if ! command -v autopoint >/dev/null && [[ -f /usr/share/gettext/config.rpath && -f /usr/share/gettext/po/Makefile.in.in ]]; then
    cp /usr/share/gettext/config.rpath .
    cp /usr/share/gettext/po/Makefile.in.in po/
    export AUTOPOINT=true
fi
run_stage bootstrap ./autogen.sh
run_stage configure ./configure --disable-international --disable-svnversion \
    --disable-filters --disable-xarlib "${@}"
run_stage compile make -j"${XARA_BUILD_JOBS:-2}"
printf 'Built executable: %s/source/XaraLX\n' "$run_dir"
echo 'Launch from its source directory and capture output; GUI functionality requires separate testing.'
