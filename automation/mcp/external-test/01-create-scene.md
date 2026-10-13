This is a manual integration test of the local Project Vengeance Xara MCP server.
Use only its three MCP tools: xara_get_capabilities, xara_get_last_result,
and xara_create_scene. Do not use shell, code execution, file tools, browser,
other connectors or sub-agents. Do not edit source/configuration or launch
scripts directly. If MCP tools are unavailable, report that and stop; never
substitute the Python smoke client or claim a tool ran when it did not.

1. Call xara_get_capabilities and confirm stdio, Automation API 0.2 and the
   one-request-per-new-editor session model.
2. Call xara_get_last_result. On a fresh server, expect no_session. If an editor
   is already active, stop; do not launch another or kill it.
3. Call xara_create_scene exactly once with the JSON arguments below. Do not
   modify the scene or add save/export/file access commands.
4. Report the tool's actual status, session_id, model_verified and evidence path.
   A starting result may be polled with xara_get_last_result. On error, stop and
   report the error; do not blindly retry a create call that may have succeeded.
5. Pause and wait for me to inspect/editability-test the scene, Undo/Redo it and
   close Xara normally. Do not call another create tool or claim visual/Undo
   success yourself. Keep this same client conversation/server alive.

Tool arguments (plain JSON, not code and not a path):

```json
{
  "scene": {
    "transaction": true,
    "requests": [
      {
        "method": "document.list_objects",
        "params": {}
      },
      {
        "method": "object.create_rectangle",
        "params": {
          "x": 72000,
          "y": 180000,
          "width": 180000,
          "height": 108000,
          "fill": "#185ADB",
          "stroke": "none"
        }
      },
      {
        "method": "object.create_ellipse",
        "params": {
          "x": 288000,
          "y": 180000,
          "width": 108000,
          "height": 108000,
          "fill": "#D4AF37",
          "stroke": "none"
        }
      },
      {
        "method": "object.create_line",
        "params": {
          "x": 72000,
          "y": 144000,
          "x2": 396000,
          "y2": 108000,
          "stroke": "#E53935",
          "stroke_width": 3000
        }
      },
      {
        "method": "object.create_text",
        "params": {
          "x": 72000,
          "y": 54000,
          "text": "PROJECT VENGEANCE",
          "font_size": 24000,
          "fill": "#202020",
          "stroke": "none"
        }
      },
      {
        "method": "document.list_objects",
        "params": {}
      }
    ]
  }
}
```
