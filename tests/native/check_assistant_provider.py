#!/usr/bin/env python3
import sys
sys.dont_write_bytecode=True
from pathlib import Path
import copy,json,unittest,subprocess,xml.etree.ElementTree as ET
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'automation/assistant'))
import provider as p
SCENE={'requests':[{'method':'object.create_ellipse','params':{'x':72000,'y':180000,'width':72000,'height':72000,'fill':'#FF0000','stroke':'none','stroke_width':None}},
 {'method':'object.create_rectangle','params':{'x':180000,'y':180000,'width':72000,'height':72000,'fill':'#0000FF','stroke':'none','stroke_width':None}}]}
class Tests(unittest.TestCase):
 def test_scene_and_transport(self):
  c=p.validate_scene(SCENE);self.assertTrue(c['transaction']);self.assertEqual(len(c['requests']),2)
  self.assertNotIn('stroke_width',c['requests'][0]['params']);self.assertEqual(ET.fromstring(p.api.encode(c['requests'],True)).get('transaction'),'true')
 def test_reject(self):
  for field,value in [('method','object.delete'),('command','rm anything')]:
   s=copy.deepcopy(SCENE);s['requests'][0][field]=value
   with self.assertRaises(p.ProviderError):p.validate_scene(s)
  for field,value in [('x',1.5),('x',1.0),('x',True),('width',0),('fill','red'),('stroke_width',False)]:
   s=copy.deepcopy(SCENE);s['requests'][0]['params'][field]=value
   with self.assertRaises(p.ProviderError):p.validate_scene(s)
 def test_bad_json(self):
  for v in ('{"x":1,"x":2}','{"x":NaN}','```json\n{}\n```','{'):
   with self.assertRaises(p.ProviderError):p.loads(v)
 def test_endpoint(self):
  for url in ('http://example.com','https://user:key@example.com','file:///tmp/a','https://example.com/?key=x'):
   with self.assertRaises(p.ProviderError):p.endpoint(url)
  self.assertEqual(p.endpoint('http://127.0.0.1:11434',True),'http://127.0.0.1:11434')
  with self.assertRaises(p.ProviderError):p.endpoint('https://example.com',True)
 def test_ollama_envelope(self):
  with patch.object(p,'post',return_value={'done':True,'message':{'content':json.dumps(SCENE)}}) as post:
   out=p.generate({'provider':'ollama','endpoint':'http://localhost:11434','model':'test'},'Draw a red circle and blue rectangle')
   self.assertEqual(len(out['requests']),2);self.assertEqual(post.call_args.args[0],'http://localhost:11434/api/chat')
   self.assertFalse(post.call_args.args[1]['stream']);self.assertEqual(post.call_args.args[1]['format'],p.SCHEMA)
 def test_ollama_errors(self):
  for reply in ({'done':False},{'done':True,'message':{'content':'bad'}},{'done':True,'done_reason':'length','message':{'content':json.dumps(SCENE)}}):
   with patch.object(p,'post',return_value=reply),self.assertRaises(p.ProviderError):p.generate({'provider':'ollama','endpoint':'http://localhost:11434','model':'test'},'circle')
 def test_compatible_tool(self):
  call={'type':'function','function':{'name':'submit_scene','arguments':json.dumps(SCENE)}}
  for calls in ([call],[call,call],[]):
   reply={'choices':[{'finish_reason':'tool_calls','message':{'tool_calls':calls}}]}
   with patch.object(p,'post',return_value=reply):
    if len(calls)==1:self.assertEqual(len(p.generate({'provider':'openai-compatible','endpoint':'https://example.com/v1','model':'test'},'draw')['requests']),2)
    else:
     with self.assertRaises(p.ProviderError):p.generate({'provider':'openai-compatible','endpoint':'https://example.com/v1','model':'test'},'draw')
 def test_connection(self):
  import urllib.error
  with patch('urllib.request.OpenerDirector.open',side_effect=urllib.error.URLError('secret server body')):
   with self.assertRaisesRegex(p.ProviderError,'^connection_or_timeout$'):p.post('http://localhost:1',{})
 def test_anthropic(self):
  with self.assertRaisesRegex(p.ProviderError,'anthropic_not_implemented'):p.generate({'provider':'anthropic','endpoint':'https://example.com','model':'test'},'draw')
 def test_worker_success(self):
  sys.path.insert(0,str(ROOT/'automation/assistant'))
  import worker,io
  class Streams:
   def __init__(self,value=b''):self.buffer=io.BytesIO(value)
  data=b'<generate><provider>ollama</provider><endpoint>http://localhost:11434</endpoint><model>test</model><key/><prompt>red circle and blue rectangle</prompt></generate>'
  output=Streams()
  with patch.object(worker.sys,'stdin',Streams(data)),patch.object(worker.sys,'stdout',output),patch.object(p,'post',return_value={'done':True,'message':{'content':json.dumps(SCENE)}}):
   self.assertEqual(worker.main(),0)
  root=ET.fromstring(output.buffer.getvalue());self.assertEqual(root.tag,'requests');self.assertEqual(root.get('transaction'),'true');self.assertEqual(len(root),2)
  self.assertEqual(root[0].get('fill'),'#FF0000');self.assertEqual(root[1].get('fill'),'#0000FF')
 def test_response_limits_and_redirect(self):
  class Reply:
   def __enter__(self):return self
   def __exit__(self,*a):pass
   def read(self,n):return b'x'*n
  with patch('urllib.request.OpenerDirector.open',return_value=Reply()),self.assertRaisesRegex(p.ProviderError,'response_too_large'):p.post('http://localhost:1',{})
  with self.assertRaisesRegex(p.ProviderError,'redirect_rejected'):p.NoRedirect().redirect_request(None,None,None,None,None,None)
 def test_worker_rejection_has_no_credentials(self):
  raw=b'<generate><provider>anthropic</provider><endpoint>https://example.com</endpoint><model>test</model><key>VERY_SECRET</key><prompt>draw</prompt></generate>'
  run=subprocess.run([sys.executable,str(ROOT/'automation/assistant/worker.py')],input=raw,capture_output=True)
  self.assertEqual(run.returncode,1);self.assertNotIn(b'VERY_SECRET',run.stdout+run.stderr);self.assertEqual(ET.fromstring(run.stdout).tag,'assistant_error')
if __name__=='__main__':unittest.main()
