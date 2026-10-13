"""Bounded provider adapters. Only inert scenes; no editor launch or code execution."""
import importlib.util
import json
import math
from pathlib import Path
import socket
import time
import urllib.error
import urllib.parse
import urllib.request
from jsonschema import Draft202012Validator

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('assistant_startup_api', ROOT/'scripts/automation-poc.py')
api=importlib.util.module_from_spec(spec);spec.loader.exec_module(api)
CANONICAL=json.loads((ROOT/'automation/request.schema.json').read_text())
# Provider-facing closed shape: rectangles/ellipses first; line/text remain available in demos.
PARAMS={'type':'object','additionalProperties':False,'required':['x','y','width','height','fill','stroke','stroke_width'], 'properties':{
 'x':{'type':'integer','minimum':-10000000,'maximum':10000000},
 'y':{'type':'integer','minimum':-10000000,'maximum':10000000},
 'width':{'type':'integer','minimum':1,'maximum':10000000},
 'height':{'type':'integer','minimum':1,'maximum':10000000},
 'fill':{'type':'string','pattern':'^#[0-9A-Fa-f]{6}$'},
 'stroke':{'type':'string','pattern':'^(none|#[0-9A-Fa-f]{6})$'},
 'stroke_width':{'type':['integer','null'],'minimum':1,'maximum':1000000}}}
SCHEMA={'type':'object','additionalProperties':False,'required':['requests'],'properties':{'requests':{
 'type':'array','minItems':1,'maxItems':14,'items':{'type':'object','additionalProperties':False,'required':['method','params'],
 'properties':{'method':{'type':'string','enum':['object.create_rectangle','object.create_ellipse']},'params':PARAMS}}}}}
SYSTEM=('You create editable vector objects. Return a scene using the supplied schema. '
 'Coordinates are integer millipoints: 72000 equals one inch. Use positive widths/heights '
 'around 72000 to 144000 and place objects near x=72000,y=180000, without overlapping '
 'when asked for next to. A circle is create_ellipse with equal width and height. '
 'Use explicit #RRGGBB colours, stroke=none and stroke_width=null if no outline. '
 'Only rectangles and ellipses are supported. Never return code or other operations.')
class ProviderError(ValueError):pass

def loads(data):
 try:return json.loads(data,object_pairs_hook=api.unique_keys,parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
 except (ValueError,TypeError,RecursionError):raise ProviderError('invalid_json') from None

def validate_scene(scene):
 if not Draft202012Validator(SCHEMA).is_valid(scene):raise ProviderError('unsupported_or_invalid_scene')
 if len(json.dumps(scene,allow_nan=False).encode())>65536:raise ProviderError('scene_too_large')
 # Normalization is deliberately limited to the documented nullable stroke-width field.
 requests=[]
 for r in scene['requests']:
  params=dict(r['params'])
  if params['stroke_width'] is None:del params['stroke_width']
  # jsonschema integer permits 1.0; the native contract deliberately does not.
  if any(type(params[k]) is not int for k in ('x','y','width','height')):raise ProviderError('integer_required')
  if 'stroke_width' in params and type(params['stroke_width']) is not int:raise ProviderError('integer_required')
  requests.append({'method':r['method'],'params':params})
 canonical={'transaction':True,'requests':requests}
 if not Draft202012Validator(CANONICAL).is_valid(canonical):raise ProviderError('canonical_schema_rejection')
 try:api.validate(canonical)
 except (ValueError,TypeError,OverflowError):raise ProviderError('semantic_rejection') from None
 return canonical

class NoRedirect(urllib.request.HTTPRedirectHandler):
 def redirect_request(self,*args,**kwargs):raise ProviderError('redirect_rejected')

def endpoint(base,local=False):
 u=urllib.parse.urlsplit(base)
 if u.username or u.password or u.query or u.fragment or not u.hostname:raise ProviderError('invalid_endpoint')
 if u.scheme!='https' and not (u.scheme=='http' and u.hostname in ('127.0.0.1','localhost','::1')):raise ProviderError('https_or_loopback_required')
 if local and u.hostname not in ('127.0.0.1','localhost','::1'):raise ProviderError('ollama_requires_loopback')
 return base.rstrip('/')

def post(url,payload,key='',timeout=90):
 data=json.dumps(payload,allow_nan=False).encode()
 headers={'Content-Type':'application/json'}
 if key:headers['Authorization']='Bearer '+key
 req=urllib.request.Request(url,data=data,headers=headers,method='POST')
 # No implicit proxy/environment credential forwarding to the local server.
 opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),NoRedirect())
 try:
  with opener.open(req,timeout=timeout) as response:
   body=response.read(1024*1024+1)
   if len(body)>1024*1024:raise ProviderError('response_too_large')
   return loads(body)
 except urllib.error.HTTPError as e:raise ProviderError('http_'+str(e.code)) from None
 except (urllib.error.URLError,socket.timeout,TimeoutError,OSError):raise ProviderError('connection_or_timeout') from None

class Ollama:
 def generate(self,config,prompt):
  reply=post(endpoint(config['endpoint'],True)+'/api/chat',{'model':config['model'],'stream':False,'format':SCHEMA,
   'messages':[{'role':'system','content':SYSTEM},{'role':'user','content':prompt}], 'options':{'temperature':0}},timeout=config.get('timeout',90))
  if reply.get('done') is not True or reply.get('done_reason')=='length':raise ProviderError('incomplete_response')
  message=reply.get('message',{})
  if message.get('tool_calls'):raise ProviderError('unexpected_tool_calls')
  return loads(message.get('content',''))

class OpenAICompatible:
 def generate(self,config,prompt):
  reply=post(endpoint(config['endpoint'])+'/chat/completions',{'model':config['model'],'stream':False,
   'messages':[{'role':'system','content':SYSTEM},{'role':'user','content':prompt}],
   'tools':[{'type':'function','function':{'name':'submit_scene','description':'Create a native scene','parameters':SCHEMA,'strict':True}}],
   'tool_choice':{'type':'function','function':{'name':'submit_scene'}},'parallel_tool_calls':False},config.get('key',''),config.get('timeout',90))
  choices=reply.get('choices',[])
  if len(choices)!=1 or choices[0].get('finish_reason')!='tool_calls':raise ProviderError('missing_or_incomplete_tool_call')
  calls=choices[0].get('message',{}).get('tool_calls',[])
  if len(calls)!=1 or calls[0].get('type')!='function' or calls[0].get('function',{}).get('name')!='submit_scene':raise ProviderError('unexpected_tool_call')
  return loads(calls[0]['function'].get('arguments',''))

class Anthropic:
 def generate(self,config,prompt):raise ProviderError('anthropic_not_implemented')
PROVIDERS={'ollama':Ollama(),'openai-compatible':OpenAICompatible(),'anthropic':Anthropic()}
def generate(config,prompt):
 if not isinstance(prompt,str) or not prompt.strip() or len(prompt.encode())>8192:raise ProviderError('invalid_prompt')
 if config.get('provider') not in PROVIDERS or not config.get('model') or len(config['model'])>200:raise ProviderError('invalid_configuration')
 return validate_scene(PROVIDERS[config['provider']].generate(config,prompt))
