#!/usr/bin/env python3
"""Real GUI + worker + loopback HTTP fixture; does not claim real Ollama inference."""
import argparse,json,os,re,signal,subprocess,tempfile,threading,time
from pathlib import Path
from http.server import BaseHTTPRequestHandler,HTTPServer
ROOT=Path(__file__).resolve().parents[2]
SCENE={'requests':[{'method':'object.create_ellipse','params':{'x':72000,'y':180000,'width':72000,'height':72000,'fill':'#FF0000','stroke':'none','stroke_width':None}},
 {'method':'object.create_rectangle','params':{'x':180000,'y':180000,'width':72000,'height':72000,'fill':'#0000FF','stroke':'none','stroke_width':None}}]}
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_POST(self):
  data=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
  self.server.seen.append({'path':self.path,'model':data.get('model'),'stream':data.get('stream')})
  time.sleep(1.5)
  body=json.dumps({'done':True,'done_reason':'stop','message':{'role':'assistant','content':json.dumps(SCENE)}}).encode()
  self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)));self.end_headers()
  try:self.wfile.write(body)
  except BrokenPipeError:pass

def classify_run(mode,text,exit_status):
 """Functional PASS cannot hide a failed native shutdown or assertion."""
 contracts={
  'success':r'^ASSISTANT_PROVIDER_TEST_PASS current-document colours native-objects undo redo async-heartbeat=(\d+)$',
  'stale':r'^ASSISTANT_PROVIDER_TEST_PASS stale-reply no-generated-mutation async-heartbeat=(\d+)$'}
 markers=re.findall(contracts[mode],text,re.MULTILINE)
 all_passes=re.findall(r'^ASSISTANT_PROVIDER_TEST_PASS\b.*$',text,re.MULTILINE)
 internal_failures=re.findall(r'^ASSISTANT_(?:PROVIDER_TEST|SELF_TEST)_FAIL\b.*$',text,re.MULTILINE)
 functional_reasons=[]
 if len(markers)!=1 or len(all_passes)!=1:functional_reasons.append('expected exactly one '+mode+' PASS marker')
 if markers and int(markers[0])<=5:functional_reasons.append('insufficient asynchronous GUI heartbeat')
 if internal_failures:functional_reasons.append('internal native test failure')
 warnings=[line for line in text.splitlines() if re.search(r'(?:Gtk|GLib(?:-GObject)?)-WARNING',line,re.I)]
 criticals=[line for line in text.splitlines() if re.search(r'(?:Gtk|GLib(?:-GObject)?)-CRITICAL',line,re.I)]
 assertions=[line for line in text.splitlines() if re.search(r'assert(?:ion)?.*failed|XARA_ASSERT_BACKTRACE_BEGIN',line,re.I)]
 crashes=[line for line in text.splitlines() if re.search(r'Segmentation fault|\bAborted\b|\bSIGSEGV\b',line,re.I)]
 lifecycle_reasons=[]
 if exit_status!=0:lifecycle_reasons.append('process/wrapper exit '+str(exit_status))
 if len(re.findall(r'^ASSISTANT_SELF_TEST_CLOSE_REQUEST normal frame closure$',text,re.MULTILINE))!=1:
  lifecycle_reasons.append('expected exactly one normal frame-close request')
 if criticals:lifecycle_reasons.append('GTK/GLib critical error detected')
 if assertions:lifecycle_reasons.append('native assertion detected')
 if crashes:lifecycle_reasons.append('native crash detected')
 reasons=functional_reasons+lifecycle_reasons
 return {'mode':mode,'exit':exit_status,'functional_passed':not functional_reasons,
  'process_exited_normally':exit_status==0,'lifecycle_passed':not lifecycle_reasons,
  'passed':not reasons,'async_heartbeat':int(markers[0]) if len(markers)==1 else None,
  'failure_reasons':reasons,'gtk_warnings':warnings,'gtk_criticals':criticals,
  'assertions':assertions,'crashes':crashes,'internal_failures':internal_failures}

def classify_http(requests):
 return len(requests)==2 and all(r.get('path')=='/api/chat' and r.get('model')=='mock-model' and r.get('stream') is False for r in requests)

def report_results(evidence,results,requests,name='status.json'):
 http_passed=classify_http(requests)
 report={'results':results,'http_requests':requests,'http_contract_passed':http_passed,
  'passed':http_passed and all(r['passed'] for r in results),'real_ollama':False}
 (evidence/name).write_text(json.dumps(report,indent=2)+'\n')
 for result in results:
  print(result['mode']+': functional '+('PASS' if result['functional_passed'] else 'FAIL')+
   ', lifecycle '+('PASS' if result['lifecycle_passed'] else 'FAIL'))
  for reason in result['failure_reasons']:print('  FAIL: '+reason)
  if result['gtk_warnings']:print('  GTK warnings retained separately: '+str(len(result['gtk_warnings'])))
 if not http_passed:print('FAIL: expected two valid non-streaming mock /api/chat requests')
 return report

def main():
 p=argparse.ArgumentParser();p.add_argument('binary',type=Path,nargs='?');p.add_argument('--data-dir',type=Path,default=ROOT)
 p.add_argument('--recheck-evidence',type=Path,help='Reclassify saved host logs; does not launch Xara or contact a server')
 p.add_argument('--debug-shutdown',action='store_true',help='Host diagnostic: GDB stops on fatal GTK criticals and prints a stack; never a passing smoke run')
 a=p.parse_args()
 if a.recheck_evidence:
  evidence=a.recheck_evidence;old=json.loads((evidence/'status.json').read_text())
  saved={r['mode']:r for r in old['results']}
  results=[classify_run(mode,(evidence/(mode+'.log')).read_text(errors='replace'),saved[mode]['exit']) for mode in ('success','stale')]
  report=report_results(evidence,results,old['http_requests'],'status-rechecked.json')
  print('Reclassified preserved logs only; no native rerun. Report:',evidence/'status-rechecked.json')
  return 0 if report['passed'] else 1
 if a.binary is None:p.error('binary is required unless --recheck-evidence is used')
 evidence=Path(tempfile.mkdtemp(prefix='xara-provider-smoke.'));print('Provider smoke evidence:',evidence,flush=True)
 try:server=HTTPServer(('127.0.0.1',0),Handler)
 except PermissionError:raise SystemExit('BLOCKED: sandbox forbids loopback sockets; run this test on the host.')
 server.seen=[];thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
 try:
  results=[]
  for mode in ('success','stale'):
   settings=evidence/mode;settings.mkdir()
   env=dict(os.environ,XARA_AI_ASSISTANT='1',XARA_ASSISTANT_PROVIDER_SELF_TEST='1',XARA_OLLAMA_MODEL='mock-model',
    XARA_OLLAMA_TEST_ENDPOINT=f'http://127.0.0.1:{server.server_port}',XARA_ASSISTANT_ROOT=str(ROOT),XARA_ASSISTANT_PYTHON=os.sys.executable,
    XARA_CONFIG_DIR=str(settings),XARA_DATA_DIR=str(a.data_dir),XARA_ASSERT_TRACE='1')
   for k in ('XARA_ASSISTANT_SELF_TEST','XARA_ASSISTANT_PROVIDER_TEST_STALE','XARA_AUTOMATION_POC_REQUEST','XARA_AUTOMATION_POC_RESPONSE'):env.pop(k,None)
   if mode=='stale':env['XARA_ASSISTANT_PROVIDER_TEST_STALE']='1'
   log=evidence/(mode+'.log')
   with log.open('wb') as f:
    command=[str(a.binary.resolve())]
    if a.debug_shutdown:
     env['G_DEBUG']='fatal-criticals'
     command=['gdb','--batch','--return-child-result','-ex','set pagination off',
      '-ex','set print frame-arguments none','-ex','set debuginfod enabled off',
      '-ex','run','-ex','thread apply all bt 40','-ex','quit','--args',*command]
    r=subprocess.Popen(['xvfb-run','-a','-s','-screen 0 1280x900x24',*command],env=env,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
    try:r.wait(timeout=90 if a.debug_shutdown else 40)
    except subprocess.TimeoutExpired:
     os.killpg(r.pid,signal.SIGTERM);r.wait(timeout=5)
     raise SystemExit('FAIL: startup/provider timeout; inspect '+str(log))
   text=log.read_text(errors='replace')
   results.append(classify_run(mode,text,r.returncode))
  report=report_results(evidence,results,server.seen)
  if a.debug_shutdown:
   print('DIAGNOSTIC: inspect success.log and stale.log for the GTK critical stack. Not a passing GUI smoke test.')
   return 1
  if not report['passed']:raise SystemExit('FAIL: native provider tests; inspect '+str(evidence))
  print('PASS: mock Ollama HTTP -> worker -> current document, styles, undo/redo, responsive heartbeat, stale reply rejection, normal close.')
 finally:server.shutdown();server.server_close()
if __name__=='__main__':raise SystemExit(main())
