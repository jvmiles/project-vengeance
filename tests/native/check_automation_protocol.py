#!/usr/bin/env python3
"""Test strict transport validation/encoding, not pretend GUI mutation coverage."""
import importlib.util,json,pathlib,sys,unittest,xml.etree.ElementTree as ET
sys.dont_write_bytecode = True
root=pathlib.Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('automation_poc',root/'scripts/automation-poc.py')
api=importlib.util.module_from_spec(spec);spec.loader.exec_module(api)
class ProtocolTests(unittest.TestCase):
 def test_sample_and_encoding(self):
  requests=api.validate(json.loads((root/'tests/native/fixtures/automation-rectangle.json').read_text()))
  xml=ET.fromstring(api.encode(requests));self.assertEqual(len(xml),3)
  self.assertEqual(xml[1].get('width'),'144000')
 def test_circle_and_ellipse(self):
  requests=api.validate(json.loads((root/'tests/native/fixtures/automation-circle.json').read_text()))
  self.assertEqual(requests[1]['method'],'object.create_ellipse')
  self.assertEqual(requests[1]['params']['width'],requests[1]['params']['height'])
  xml=ET.fromstring(api.encode(requests));self.assertEqual(xml[1].get('method'),'object.create_ellipse')
  for value in [True,0,-1,'code']:
   params=dict(requests[1]['params']);params['width']=value
   with self.subTest(value=value),self.assertRaises(ValueError):
    api.validate({'requests':[{'method':'object.create_ellipse','params':params}]})
 def test_scene_and_text_transport(self):
  value=json.loads((root/'tests/native/fixtures/automation-scene.json').read_text())
  requests=api.validate(value);xml=ET.fromstring(api.encode(requests,value['transaction']))
  self.assertEqual(xml.get('transaction'),'true');self.assertEqual(len(xml),6)
  self.assertEqual(bytes.fromhex(xml[4].get('text_hex')).decode('ascii'),'PROJECT VENGEANCE')
  special={'requests':[{'method':'object.create_text','params':{'x':0,'y':0,'text':'A & "B" <C>','font_size':24000}}]}
  encoded=api.encode(api.validate(special))
  self.assertNotIn(b'&',encoded);self.assertEqual(bytes.fromhex(ET.fromstring(encoded)[0].get('text_hex')).decode(),'A & "B" <C>')
 def test_invalid_scene_parameters(self):
  invalid=[]
  for key,value in [('text',''),('text','a\n'),('text','ä'),('font_size',True),('font_size',999),('fill','#123'),('stroke','red'),('stroke_width',0),('width',10)]:
   p={'x':0,'y':0,'text':'abc','font_size':24000};p[key]=value
   invalid.append({'requests':[{'method':'object.create_text','params':p}]})
  invalid += [ {'requests':[{'method':'object.create_line','params':{'x':0,'y':0,'x2':0,'y2':0}}]},
    {'requests':[{'method':'object.create_line','params':{'x':0,'y':0,'x2':10,'y2':10,'fill':'#FFFFFF'}}]},
    {'transaction':'true','requests':[{'method':'document.list_objects','params':{}}]},
    {'transaction':True,'requests':[{'method':'document.list_objects','params':{}}]} ]
  scene=json.loads((root/'tests/native/fixtures/automation-scene.json').read_text());scene['requests'].insert(3,{'method':'document.list_objects','params':{}});invalid.append(scene)
  for value in invalid:
   with self.subTest(value=value),self.assertRaises(ValueError):api.validate(value)
 def test_snapshot_verification_rejects_wrong_scene(self):
  request={'method':'object.create_line','params':{'x':1,'y':2,'x2':3,'y2':4,'stroke':'#E53935','stroke_width':3000}}
  result={'method':'object.create_line','ok':True,'created_tag':'57','objects':[{'id':'node:57','type':'NodePath','stroke':'#E53935','stroke_width':3000,'anchor_x':1,'anchor_y':2,'end_x':3,'end_y':4}]}
  api.verify_results([request],[result])
  for key,value in [('type','NodeRect'),('stroke','#000000'),('end_x',999),('stroke_width',1)]:
   broken=json.loads(json.dumps(result));broken['objects'][0][key]=value
   with self.subTest(key=key),self.assertRaises(ValueError):api.verify_results([request],[broken])
 def test_complete_scene_snapshot_checks(self):
  requests=api.validate(json.loads((root/'tests/native/fixtures/automation-scene.json').read_text()))
  objects=[];results=[{'method':'document.list_objects','ok':True,'objects':[]}]
  names=['NodeRect','NodeEllipse','NodePath','TextStory']
  for i,request in enumerate(requests[1:-1]):
   p=request['params'];obj={'id':f'node:{i+1}','type':names[i],'selected':i==3,'bounds':{'x0':p['x'],'y0':p['y'],'x1':p['x']+p.get('width',0),'y1':p['y']+p.get('height',0)}}
   for key in ['fill','stroke','stroke_width','text','font_size']:
    if key in p:obj[key]=p[key]
   obj.update(anchor_x=p['x'],anchor_y=p['y'],end_x=p.get('x2',0),end_y=p.get('y2',0))
   objects.append(obj);results.append({'method':request['method'],'ok':True,'created_tag':str(i+1),'objects':[obj]})
  results.append({'method':'document.list_objects','ok':True,'objects':objects})
  api.verify_results(requests,results)
  for defect in ['count','text','colour','geometry','selection']:
   broken=json.loads(json.dumps(results));o=broken[-1]['objects']
   if defect=='count':o.pop()
   elif defect=='text':o[-1]['text']='wrong'
   elif defect=='colour':o[0]['fill']='#FFFFFF'
   elif defect=='geometry':o[0]['bounds']['x1']+=1000
   else:o[-1]['selected']=False
   with self.subTest(defect=defect),self.assertRaises(ValueError):api.verify_results(requests,broken)
 def test_invalid_requests(self):
  base={'x':0,'y':0,'width':10,'height':10}
  invalid=[{}, {'requests':[]},{'requests':[{'method':'object.delete','params':{}}]},
   {'requests':[{'method':'document.list_objects','params':{'path':'/etc/passwd'}}]}]
  for key,value in [('width',True),('width',0),('height',-1),('x',10000000),('x','__import__("os")')]:
   params=dict(base);params[key]=value
   invalid.append({'requests':[{'method':'object.create_rectangle','params':params}]})
  invalid.append({'requests':[{'method':'document.list_objects','params':{}}]*17})
  for value in invalid:
   with self.subTest(value=value),self.assertRaises(ValueError):api.validate(value)
 def test_duplicate_keys(self):
  with self.assertRaises(ValueError):json.loads('{"method":"a","method":"b"}',object_pairs_hook=api.unique_keys)
 def test_response(self):
  r=api.decode(b'<automation_response><result index="0" method="document.list_objects" ok="true"><object tag="42" type="NodeRect" selected="true" x0="0" y0="0" x1="10" y1="20"/></result></automation_response>')
  self.assertEqual(r[0]['objects'][0]['id'],'node:42')
  self.assertEqual(r[0]['objects'][0]['bounds']['y1'],20)
  with self.assertRaises(ValueError):api.decode(b'<automation_response error="invalid_request"/>')
if __name__=='__main__':unittest.main()
