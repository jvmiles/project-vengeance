"""Opt-in loopback diagnostic relay; forwards real requests, never supplies scenes."""
import json,os,tempfile,threading
from pathlib import Path
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import provider
from trace_real_worker import save
ENDPOINT='http://127.0.0.1:11435'
UPSTREAM='http://127.0.0.1:11434/api/chat'
MODEL='qwen2.5-coder:7b'

def check_request(path,payload,authorization=None):
 if path!='/api/chat' or authorization or not isinstance(payload,dict) or payload.get('model')!=MODEL or payload.get('stream') is not False:
  raise provider.ProviderError('diagnostic_request_rejected')

def start(evidence):
 evidence=Path(evidence)
 if evidence.stat().st_uid!=os.getuid() or evidence.stat().st_mode&0o077:
  raise ValueError('Private owned evidence directory required')
 class Handler(BaseHTTPRequestHandler):
  def log_message(self,*args):pass
  def do_POST(self):
   directory=Path(tempfile.mkdtemp(prefix='http-',dir=evidence))
   try:
    size=int(self.headers.get('Content-Length','0'))
    if self.headers.get('Transfer-Encoding') or not 0<size<=65536:raise provider.ProviderError('diagnostic_request_size')
    payload=provider.loads(self.rfile.read(size))
    check_request(self.path,payload,self.headers.get('Authorization'))
    save(directory,'request.json',{'path':self.path,'upstream':UPSTREAM,'model':payload['model'],'payload':payload,'mock':False})
    print('REAL_HTTP_REQUEST_RECEIVED POST /api/chat model='+MODEL+'; forwarding to real Ollama :11434; evidence='+directory.name,flush=True)
    # Reuse original bounded transport with fixed real upstream, no response substitution.
    reply=provider.post(UPSTREAM,payload)
    save(directory,'response.json',reply)
    print('REAL_HTTP_RESPONSE_RECORDED evidence='+directory.name+'; worker/GUI validation still required',flush=True)
    validation={'diagnostic_only':True,'xara_worker_validation_proven':False}
    try:
     scene=provider.validate_scene(provider.loads(reply['message']['content']))
     save(directory,'diagnostic-scene.json',scene);validation['schema_valid']=True
    except Exception:validation['schema_valid']=False
    save(directory,'diagnostic-validation.json',validation)
    body=json.dumps(reply,allow_nan=False).encode()
    self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
   except Exception as error:
    save(directory,'error.json',{'error_type':type(error).__name__})
    try:self.send_error(502,'Diagnostic relay could not forward request')
    except (BrokenPipeError,ConnectionResetError):pass
 server=ThreadingHTTPServer(('127.0.0.1',11435),Handler)
 server.daemon_threads=True
 thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
 return server
