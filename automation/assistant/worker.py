#!/usr/bin/env python3
"""One bounded stdin request, one stdout XML scene. Credentials never in argv/logs."""
import sys
sys.dont_write_bytecode=True
import xml.etree.ElementTree as ET
from provider import generate, ProviderError, api

def main():
 try:
  raw=sys.stdin.buffer.read(32769)
  if len(raw)>32768 or b'<!' in raw:raise ProviderError('invalid_worker_input')
  root=ET.fromstring(raw)
  if root.tag!='generate' or root.attrib or len(root)!=5:raise ProviderError('invalid_worker_input')
  fields={}
  for n in root:
   if n.tag not in ('provider','endpoint','model','key','prompt') or n.tag in fields or n.attrib or len(n):raise ProviderError('invalid_worker_input')
   fields[n.tag]=n.text or ''
  scene=generate(fields,fields.pop('prompt'))
  sys.stdout.buffer.write(api.encode(scene['requests'],True));return 0
 except ProviderError as error:
  # All ProviderError strings are locally assigned codes, never service bodies.
  out=ET.Element('assistant_error',code=str(error));sys.stdout.buffer.write(ET.tostring(out));return 1
 except Exception:
  sys.stdout.buffer.write(b'<assistant_error code="provider_or_validation_failure"/>');return 1
if __name__=='__main__':raise SystemExit(main())
