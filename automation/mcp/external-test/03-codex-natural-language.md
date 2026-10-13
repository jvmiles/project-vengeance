Use only the local Xara MCP tools for this drawing test. Do not use shell,
Python, file tools, browser, other connectors or sub-agents; do not edit any
source/configuration or run the automation helper directly. If the MCP tools
are unavailable, report that and stop.

First call xara_get_capabilities and inspect its automation schema and limits.
Then call xara_get_last_result; expect no_session in this fresh server. If a
session is active, stop instead of opening another editor or terminating it.

Create this composition in a new Xara document:
- A blue rectangle (#185ADB), 2.5 inches wide and 1.5 inches high, with its
  bottom-left corner at x=1 inch, y=2.5 inches. No outline.
- A gold circle (#D4AF37), diameter 1.5 inches, with the bottom-left corner of
  its bounding box at x=4 inches, y=2.5 inches. No outline.
- A red diagonal line (#E53935), 3-point stroke, from (1 inch, 2 inches) to
  (5.5 inches, 1.5 inches).
- Editable dark text (#202020), "PROJECT VENGEANCE", at a 24-point font size,
  with baseline anchor x=1 inch, y=0.75 inches. No outline; use the default font.

Interpret these as spread-local document coordinates with positive y upwards.
Convert dimensions to the millipoints required by the schema. Use a shared
creation transaction so a single Undo removes the composition and Redo restores
it. Include an initial and final object-list request. Build the JSON yourself
from this natural-language description and call xara_create_scene exactly once.
Never supply shell/code/path fields. Do not retry creation after an ambiguous
result; query xara_get_last_result instead.

Report the real tool status, session_id, model_verified, final object types and
evidence_directory. If starting, poll the read tool briefly. Then pause and wait
for me to check the visible scene, editability, Undo/Redo and close Xara normally.
Do not claim visual checks yourself. Keep this conversation/server alive; do not
create another scene or try to automate GUI shutdown.
