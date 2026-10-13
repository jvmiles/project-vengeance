#!/usr/bin/env python3
"""Protocol-test double only: does NOT render or exercise Xara, CDraw or undo."""
import copy
import os
from pathlib import Path
import time
import xml.etree.ElementTree as ET

request = ET.parse(os.environ['XARA_AUTOMATION_POC_REQUEST']).getroot()
response = ET.Element('automation_response')
objects = []
for index, command in enumerate(request):
    p = command.attrib
    method = p['method']
    result = ET.SubElement(response, 'result', index=str(index), method=method, ok='true')
    if method == 'document.list_objects':
        for obj in objects: ET.SubElement(result, 'object', **obj)
        continue
    for obj in objects: obj['selected'] = 'false'
    kind = {'object.create_rectangle':'NodeRect', 'object.create_ellipse':'NodeEllipse',
            'object.create_line':'NodePath', 'object.create_text':'TextStory'}[method]
    x,y=int(p['x']),int(p['y'])
    obj = dict(tag=str(57+len(objects)), type=kind, selected='true',
               x0=str(x), y0=str(y), x1=str(x+int(p.get('width','0'))),
               y1=str(y+int(p.get('height','0'))), fill=p.get('fill','#000000').upper(),
               stroke=p.get('stroke','#000000').upper(), stroke_width=p.get('stroke_width','500'),
               anchor_x=str(x), anchor_y=str(y), end_x=p.get('x2','0'),end_y=p.get('y2','0'),
               font_size=p.get('font_size','0'), text=bytes.fromhex(p.get('text_hex','')).decode('ascii'))
    # ColourString emits the transparent keyword in lowercase.
    for name in ('fill','stroke'):
        if obj[name] == 'NONE': obj[name] = 'none'
    objects.append(obj)
    result.set('created_tag',obj['tag']);ET.SubElement(result,'object',**copy.deepcopy(obj))
path = Path(os.environ['XARA_AUTOMATION_POC_RESPONSE'])
with path.open('xb') as stream: stream.write(ET.tostring(response))
release = Path(os.environ['XARA_MCP_TEST_RELEASE'])
deadline = time.monotonic()+20
while not release.exists() and time.monotonic()<deadline: time.sleep(0.05)
