#!/usr/bin/env bash
# Fixed interpreter wrapper used only by the opt-in real-model acceptance test.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
: "${XARA_ASSISTANT_REAL_PYTHON:?Set the pinned Python interpreter}"
exec "$XARA_ASSISTANT_REAL_PYTHON" "$root/automation/assistant/trace_real_worker.py" "$@"
