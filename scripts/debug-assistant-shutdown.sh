#!/usr/bin/env bash
# Host-only isolated mock sessions; no renderer/system/display permission changes.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
: "${XARA_ASSISTANT_PYTHON:?Set the Python environment executable containing jsonschema}"
binary=${1:?Provide the experimental debug executable path}
command -v gdb >/dev/null
command -v xvfb-run >/dev/null
exec "$XARA_ASSISTANT_PYTHON" "$root/tests/native/smoke_assistant_provider.py" \
 "$binary" --data-dir "${XARA_DATA_DIR:-$root}" --debug-shutdown
