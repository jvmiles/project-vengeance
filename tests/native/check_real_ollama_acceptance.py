#!/usr/bin/env python3
"""Acceptance tooling unit tests; do not constitute real inference evidence."""
import importlib.util,io,json,os,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
def module(name,path):
 spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
accept=module('accept',ROOT/'scripts/test-real-ollama.py')
trace=module('trace',ROOT/'automation/assistant/trace_real_worker.py')
p=accept.provider
SCENE={'requests':[{'method':'object.create_ellipse','params':{'x':72000,'y':180000,'width':72000,'height':72000,'fill':'#FF0000','stroke':'none','stroke_width':None}},
 {'method':'object.create_rectangle','params':{'x':180000,'y':180000,'width':108000,'height':72000,'fill':'#0000FF','stroke':'none','stroke_width':None}}]}
class Tests(unittest.TestCase):
 def test_actual_wrapper_subprocess(self):
  env=dict(os.environ,XARA_ASSISTANT_PYTHON=str(ROOT/'scripts/trace-real-ollama-worker.sh'),
   XARA_ASSISTANT_REAL_PYTHON=sys.executable)
  self.assertTrue(accept.recorder_preflight(env)['passed'])
 def test_dispatch_trace_is_opt_in(self):
  with patch.object(accept.shutil,'which',return_value='/usr/bin/strace'):
   self.assertEqual(accept.launch_command('/fixed/XaraLX',Path('/evidence')),['/fixed/XaraLX'])
   cmd=accept.launch_command('/fixed/XaraLX',Path('/evidence'),True)
   self.assertIn('trace=execve,connect',cmd);self.assertEqual(cmd[-1],'/fixed/XaraLX')
  with patch.object(accept.shutil,'which',return_value=None),self.assertRaises(RuntimeError):
   accept.launch_command('/fixed/XaraLX',Path('/evidence'),True)
 def test_relay_contract(self):
  import capture_ollama_http as relay
  payload={'model':'qwen2.5-coder:7b','stream':False}
  relay.check_request('/api/chat',payload)
  for path,body,key in [('/api/generate',payload,None),('/api/chat',dict(payload,model='other'),None),('/api/chat',dict(payload,stream=True),None),('/api/chat',payload,'secret')]:
   with self.assertRaises(p.ProviderError):relay.check_request(path,body,key)
 def test_explicit_binary_pin(self):
  import hashlib
  with tempfile.TemporaryDirectory() as tmp:
   binary=Path(tmp)/'XaraLX';binary.write_bytes(b'integration-test')
   expected=hashlib.sha256(binary.read_bytes()).hexdigest()
   self.assertEqual(accept.verify_binary_hash(binary,expected),expected)
   with self.assertRaises(ValueError):accept.verify_binary_hash(binary)
   with self.assertRaises(ValueError):accept.verify_binary_hash(binary,'not-a-hash')
   binary.write_bytes(b'changed')
   with self.assertRaises(ValueError):accept.verify_binary_hash(binary,expected)
 def test_launch_metadata_does_not_claim_provider_selection(self):
  env={'XARA_ASSISTANT_ROOT':'/fixed/root','XARA_ASSISTANT_PYTHON':'/fixed/worker','PRIVATE_KEY':'never-record'}
  meta=accept.launch_metadata(Path('/fixed/XaraLX'),['/fixed/XaraLX'],env,'http://127.0.0.1:11435')
  self.assertEqual(meta['initial_panel']['provider'],'Predefined demo')
  self.assertEqual(meta['required_panel']['provider'],'Ollama / local')
  self.assertFalse(meta['actual_panel_selection_recorded'])
  self.assertNotIn('never-record',json.dumps(meta))
 def test_geometry_contract(self):
  scene=p.validate_scene(SCENE);self.assertTrue(accept.geometry_checks(scene))
  scene['requests'][0]['params']['height']=71000;self.assertFalse(accept.geometry_checks(scene))
 def test_malformed_rejection(self):
  result=accept.fault_checks(SCENE);self.assertEqual(len(result),6);self.assertTrue(all(result.values()))
 def test_colour_order_case(self):
  scene=p.validate_scene(SCENE);scene['requests'].reverse();scene['requests'][0]['params']['fill']='#0000ff';self.assertTrue(accept.geometry_checks(scene))
 def test_fixed_worker_path(self):
  with patch.object(sys,'argv',['trace','/tmp/not-the-worker.py']),self.assertRaises(SystemExit):trace.main()
 def test_private_evidence_required(self):
  with tempfile.TemporaryDirectory() as t:
   os.chmod(t,0o755)
   with patch.object(sys,'argv',['trace',str(ROOT/'automation/assistant/worker.py')]),patch.dict(os.environ,XARA_OLLAMA_EVIDENCE_DIR=t),self.assertRaises(SystemExit):trace.main()
 def test_recording_preserves_worker_xml(self):
  import worker
  class Streams:
   def __init__(self,b=b''):self.buffer=io.BytesIO(b)
  raw=('<generate><provider>ollama</provider><endpoint>http://127.0.0.1:11434</endpoint><model>qwen2.5-coder:7b</model><key/><prompt>draw</prompt></generate>').encode()
  original_validate=p.validate_scene
  with tempfile.TemporaryDirectory() as t:
   output=Streams()
   with patch.object(sys,'argv',['trace',str(ROOT/'automation/assistant/worker.py')]),patch.dict(os.environ,XARA_OLLAMA_EVIDENCE_DIR=t),patch.object(sys,'stdin',Streams(raw)),patch.object(sys,'stdout',output),patch.object(p,'post',return_value={'done':True,'message':{'content':json.dumps(SCENE)}}) as transport:
    try:self.assertEqual(trace.main(),0)
    finally:p.validate_scene=original_validate # recorder is deliberately process-local
   import xml.etree.ElementTree as ET
   root=ET.fromstring(output.buffer.getvalue());self.assertEqual(root.tag,'requests');self.assertEqual(len(root),2)
   directory=next(Path(t).glob('inference-*'));self.assertTrue((directory/'response.json').exists());self.assertTrue((directory/'validated-scene.json').exists())
   self.assertEqual(transport.call_args.args[0],'http://127.0.0.1:11434/api/chat')
   self.assertEqual((directory/'response.json').stat().st_mode&0o777,0o600)
if __name__=='__main__':unittest.main()
