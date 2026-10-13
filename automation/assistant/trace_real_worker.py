#!/usr/bin/env python3
"""Opt-in host test recorder around the UNCHANGED real worker, never a mock provider."""
import sys
sys.dont_write_bytecode=True
import hashlib,json,os
from pathlib import Path
import tempfile
import xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
ENDPOINT=('http://127.0.0.1:11435/api/chat' if os.environ.get('XARA_OLLAMA_CAPTURE_HTTP')=='1' else 'http://127.0.0.1:11434/api/chat')
MODEL='qwen2.5-coder:7b'

def save(directory,name,value):
 path=directory/name
 fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
 with os.fdopen(fd,'w') as stream:json.dump(value,stream,indent=2,allow_nan=False);stream.write('\n')

def main():
 if len(sys.argv)!=2 or Path(sys.argv[1]).resolve()!=ROOT/'automation/assistant/worker.py':
  raise SystemExit('Only the fixed Xara provider worker is permitted')
 evidence=Path(os.environ['XARA_OLLAMA_EVIDENCE_DIR']).resolve()
 if not evidence.is_dir() or evidence.stat().st_uid!=os.getuid() or evidence.stat().st_mode&0o077:
  raise SystemExit('Private evidence directory required')
 run=Path(tempfile.mkdtemp(prefix='inference-',dir=evidence))
 sys.path.insert(0,str(ROOT/'automation/assistant'))
 import provider,worker
 original_post=provider.post;original_validate=provider.validate_scene
 def traced_post(url,payload,key='',timeout=90):
  if url!=ENDPOINT or payload.get('model')!=MODEL or payload.get('stream') is not False or key:
   raise provider.ProviderError('real_test_configuration_mismatch')
  save(run,'request.json',{'endpoint':url,'real_inference':True,'payload':payload})
  # Real bounded urllib transport; no canned output, relay or mocked response.
  reply=original_post(url,payload,key,timeout)
  save(run,'response.json',reply)
  if isinstance(reply,dict):
   content=reply.get('message',{}).get('content')
   if isinstance(content,str):save(run,'model-output.json',{'content':content})
  return reply
 def traced_validate(scene):
  canonical=original_validate(scene) # Strict schema + existing semantic validation first.
  save(run,'validated-scene.json',canonical)
  return canonical
 provider.post=traced_post;provider.validate_scene=traced_validate
 save(run,'origin.json',{'real_inference':True,'mock':False,'endpoint':ENDPOINT,'model':MODEL,
  'provider_sha256':hashlib.sha256((ROOT/'automation/assistant/provider.py').read_bytes()).hexdigest()})
 result=worker.main() # Existing stdin protocol and stdout XML remain unchanged.
 save(run,'worker-status.json',{'exit':result,'validated':(run/'validated-scene.json').is_file(),'real_inference':True})
 return result
if __name__=='__main__':raise SystemExit(main())
