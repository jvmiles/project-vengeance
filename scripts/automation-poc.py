#!/usr/bin/env python3
"""Explicit startup-only shape/list demo. No server, provider or shell execution."""
import argparse, json, os, pathlib, re, subprocess, tempfile, time
import xml.etree.ElementTree as ET
ROOT = pathlib.Path(__file__).resolve().parents[1]
LIMIT = 10_000_000

def unique_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result: raise ValueError('Duplicate JSON key')
        result[key] = value
    return result

def validate(value):
    if not isinstance(value, dict) or set(value) - {'requests', 'transaction'} or 'requests' not in value:
        raise ValueError('Expected requests and optional transaction')
    if 'transaction' in value and type(value['transaction']) is not bool:
        raise ValueError('Transaction must be boolean')
    requests = value['requests']
    if not isinstance(requests, list) or not 1 <= len(requests) <= 16:
        raise ValueError('Expected 1–16 requests')
    for request in requests:
        if not isinstance(request, dict) or set(request) != {'method', 'params'}:
            raise ValueError('Expected method and params')
        params, method = request['params'], request['method']
        if not isinstance(params, dict): raise ValueError('Params must be an object')
        if method == 'document.list_objects':
            if params: raise ValueError('List takes no parameters')
            continue
        if method in ('object.create_rectangle', 'object.create_ellipse'):
            required = {'x','y','width','height'}
        elif method == 'object.create_line': required = {'x','y','x2','y2'}
        elif method == 'object.create_text': required = {'x','y','text','font_size'}
        else: raise ValueError('Unsupported method')
        optional = {'stroke','stroke_width'} | (set() if method == 'object.create_line' else {'fill'})
        if not required <= set(params) or set(params) - required - optional:
            raise ValueError('Missing or unsupported parameters')
        for key in required - {'text'}:
            v=params[key]
            if type(v) is not int or not -LIMIT <= v <= LIMIT:
                raise ValueError('Geometry must use bounded integers')
        for key in ('fill','stroke'):
            if key in params and (not isinstance(params[key],str) or
                not re.fullmatch(r'none|#[0-9a-fA-F]{6}',params[key])):
                raise ValueError('Colour must be #RRGGBB or none')
        if 'stroke_width' in params and (type(params['stroke_width']) is not int or not 1 <= params['stroke_width'] <= 1000000):
            raise ValueError('Stroke width must be 1–1000000 millipoints')
        if method in ('object.create_rectangle','object.create_ellipse'):
            if params['width'] <= 0 or params['height'] <= 0 or params['x']+params['width']>LIMIT or params['y']+params['height']>LIMIT:
                raise ValueError('Invalid shape bounds')
        elif method == 'object.create_line':
            if (params['x'],params['y']) == (params['x2'],params['y2']): raise ValueError('Zero-length line')
        else:
            text=params['text']
            if not isinstance(text,str) or not 1 <= len(text) <= 256 or any(not 32 <= ord(c) <= 126 for c in text):
                raise ValueError('Text must be 1–256 printable ASCII characters; Unicode/multiline text unsupported')
            if not 1000 <= params['font_size'] <= 1000000: raise ValueError('Invalid font size')
    if value.get('transaction'):
        methods=[r['method'] for r in requests]
        interior=methods[1:] if methods[0]=='document.list_objects' else methods[:]
        if interior and interior[-1]=='document.list_objects': interior=interior[:-1]
        if not interior or any(m=='document.list_objects' for m in interior):
            raise ValueError('Transaction allows optional list, creations, optional list')
    return requests

def encode(requests, transaction=False):
    root = ET.Element('requests', **({'transaction':'true'} if transaction else {}))
    for request in requests:
        params={k:str(v) for k,v in request['params'].items() if k!='text'}
        if 'text' in request['params']: params['text_hex']=request['params']['text'].encode('ascii').hex()
        ET.SubElement(root, 'request', method=request['method'], **params)
    return ET.tostring(root, encoding='utf-8', xml_declaration=True)

def decode(data):
    if len(data) > 4 * 1024 * 1024: raise ValueError('Response too large')
    root = ET.fromstring(data)
    if root.tag != 'automation_response' or root.get('error'): raise ValueError('Editor rejected request')
    results=[]
    for record in root:
        if record.tag != 'result': raise ValueError('Unexpected response')
        result=dict(record.attrib); result['ok'] = record.get('ok') == 'true'
        result['objects']=[]
        for item in record:
            if item.tag != 'object': raise ValueError('Unexpected object record')
            obj=dict(id='node:'+item.attrib['tag'], type=item.attrib['type'],
                selected=item.get('selected')=='true', bounds={k:int(item.attrib[k]) for k in ['x0','y0','x1','y1']})
            for key in ('fill','stroke','text'):
                if key in item.attrib: obj[key]=item.attrib[key]
            for key in ('stroke_width','font_size','anchor_x','anchor_y','end_x','end_y'):
                if key in item.attrib: obj[key]=int(item.attrib[key])
            result['objects'].append(obj)
        results.append(result)
    return results

def verify_results(requests, results):
    if len(results)!=len(requests) or not all(r['ok'] for r in results): raise ValueError('Incomplete/failed results')
    if [r.get('method') for r in results]!=[r['method'] for r in requests]: raise ValueError('Response method mismatch')
    types={'object.create_rectangle':'NodeRect','object.create_ellipse':'NodeEllipse',
           'object.create_line':'NodePath','object.create_text':'TextStory'}
    creations=[]
    for request,result in zip(requests,results):
        method=request['method']
        if method not in types: continue
        identity='node:'+result['created_tag']
        objects=results[-1]['objects'] if requests[-1]['method']=='document.list_objects' else result['objects']
        matches=[o for o in objects if o['id']==identity]
        if len(matches)!=1: raise ValueError('Missing unique created-object snapshot')
        obj=matches[0];p=request['params']
        if obj['type']!=types[method]: raise ValueError('Wrong native object type')
        for key in ('fill','stroke'):
            if key in p and obj.get(key,'').upper()!=p[key].upper(): raise ValueError('Incorrect '+key)
        if 'stroke_width' in p and obj.get('stroke_width')!=p['stroke_width']: raise ValueError('Incorrect stroke width')
        if method in ('object.create_rectangle','object.create_ellipse'):
            expected=dict(x0=p['x'],y0=p['y'],x1=p['x']+p['width'],y1=p['y']+p['height'])
            if any(abs(obj['bounds'][k]-v)>1 for k,v in expected.items()): raise ValueError('Incorrect shape geometry')
        elif method=='object.create_line':
            for key,param in [('anchor_x','x'),('anchor_y','y'),('end_x','x2'),('end_y','y2')]:
                if obj.get(key)!=p[param]: raise ValueError('Incorrect line endpoint')
        else:
            for key,param in [('anchor_x','x'),('anchor_y','y'),('text','text'),('font_size','font_size')]:
                if obj.get(key)!=p[param]: raise ValueError('Incorrect text '+param)
        creations.append(identity)
    if requests[0]['method']==requests[-1]['method']=='document.list_objects' and creations:
        before,after=results[0]['objects'],results[-1]['objects']
        if len(after)!=len(before)+len(creations): raise ValueError('Incorrect final object count')
        if not set(creations)<=set(o['id'] for o in after): raise ValueError('Objects missing from final list')
        if not any(o['id']==creations[-1] and o['selected'] for o in after): raise ValueError('Last object not selected')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=pathlib.Path,default=ROOT/'build/automation-v02/XaraLX')
    parser.add_argument('--request',type=pathlib.Path,default=ROOT/'tests/native/fixtures/automation-rectangle.json')
    parser.add_argument('--validate-only',action='store_true')
    args=parser.parse_args()
    data=args.request.read_bytes()
    if len(data)>65536: raise ValueError('Request too large')
    value=json.loads(data.decode('utf-8'),object_pairs_hook=unique_keys)
    requests=validate(value)
    if args.validate_only:
        print('PASS: allowlisted structured request validates; no application started'); return 0
    binary=args.binary.resolve()
    if not binary.is_file() or not os.access(binary,os.X_OK): raise ValueError('Automation build unavailable')
    run=pathlib.Path(tempfile.mkdtemp(prefix='xara-automation.'))
    (run/'settings').mkdir(mode=0o700)
    with (run/'request.xml').open('xb') as out: out.write(encode(requests,value.get('transaction',False)))
    (run/'request.xml').chmod(0o600)
    env=dict(os.environ,XARA_AUTOMATION_POC_REQUEST=str(run/'request.xml'),
        XARA_AUTOMATION_POC_RESPONSE=str(run/'response.xml'),XARA_CONFIG_DIR=str(run/'settings'),
        XARA_DATA_DIR=str(ROOT),XARA_ASSERT_TRACE='1',XARA_AUTOMATION_POC_FIT_DRAWING='1')
    print('Automation evidence:',run,flush=True)
    print('Objects use specified colours or current attributes; the demo fits the drawing into view. Check Undo/Redo, then close Xara normally (discard the test document if desired).',flush=True)
    with (run/'startup.log').open('xb') as log:
        process=subprocess.Popen([str(binary)],env=env,stdout=log,stderr=subprocess.STDOUT)
        results=None
        deadline=time.monotonic()+60
        while time.monotonic()<deadline:
            if (run/'response.xml').exists():
                try: results=decode((run/'response.xml').read_bytes()); break
                except ET.ParseError: pass # writer may not yet have finished
                except ValueError: break
            if process.poll() is not None: break
            time.sleep(0.1)
        if results is not None:
            (run/'result.json').write_text(json.dumps(results,indent=2)+'\n')
            print('Results:',run/'result.json',flush=True)
        else: print('No valid automation response. Inspect startup.log; close the test instance normally.',flush=True)
        status=process.wait() # Never kill a GUI with potential unsaved user changes.
    (run/'status.json').write_text(json.dumps({'application_exit':status})+'\n')
    if re.search(r'assert .*failed|XARA_ASSERT_BACKTRACE_BEGIN', (run/'startup.log').read_text(errors='replace')):
        print('FAIL: startup assertion detected; inspect startup.log'); return 1
    if status or results is None or len(results)!=len(requests) or not all(r['ok'] for r in results): return 1
    verify_results(requests, results)
    print('PASS: requested objects, geometry and specified styles match model snapshots. Visual appearance and Undo/Redo remain manual checks.')
    return 0

if __name__=='__main__':
    try: raise SystemExit(main())
    except (ValueError,OSError,KeyError) as error:
        print('Automation demo failed:',error); raise SystemExit(2)
