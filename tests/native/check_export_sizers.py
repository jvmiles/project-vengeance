#!/usr/bin/env python3
"""Check explicit grid capacities in the PNG export dialog resource."""
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

resource = Path(sys.argv[1]) if len(sys.argv) == 2 else Path(__file__).resolve().parents[2] / "wxOil/xrc/EN/bmapprev-sizer.xrc"
root = ET.parse(resource).getroot()

def local(tag):
    return tag.rsplit("}", 1)[-1]

checked = 0
failures = []
for element in root.iter():
    if local(element.tag) != "object" or element.get("class") not in ("wxGridSizer", "wxFlexGridSizer"):
        continue
    checked += 1
    children = list(element)
    values = {local(child.tag): child.text for child in children if local(child.tag) in ("rows", "cols")}
    rows, cols = int(values.get("rows", "0")), int(values.get("cols", "0"))
    items = sum(local(child.tag) == "object" for child in children)
    if rows < 0 or cols < 0 or (rows == 0 and cols == 0) or (rows and cols and items > rows * cols):
        failures.append(f"{element.get('class')}: {items} items in {rows}x{cols} grid")
if not checked:
    failures.append("No grid sizers found; test did not exercise export resource")
if failures:
    print("FAIL: " + "; ".join(failures), file=sys.stderr)
    sys.exit(1)
print(f"PASS: {checked} export grid sizers have valid capacities (GUI rendering untested)")
