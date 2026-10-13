#!/usr/bin/env bash
# Compare two differential-test CSVs (tests/native/run_cairo_differential.sh <csv>) for
# bit-identical Cairo output: same scenes, same output hash. Used to show that a core
# optimisation does not change any pixel or changed box.
# Usage: tests/native/compare_cairo_hashes.sh before.csv after.csv
set -euo pipefail
diff <(cut -d, -f1,12 "$1" | sort) <(cut -d, -f1,12 "$2" | sort) > /dev/null && {
    echo "IDENTICAL: $(($(wc -l < "$1") - 1)) runs, all output hashes equal"; exit 0; }
echo "DIFFERENT output hashes:"; diff <(cut -d, -f1,12 "$1" | sort) <(cut -d, -f1,12 "$2" | sort) | head -20; exit 1
