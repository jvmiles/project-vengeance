#!/usr/bin/env python3
"""Log-classifier regression, including the actual host shutdown critical."""
import importlib.util,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('smoke',Path(__file__).with_name('smoke_assistant_provider.py'))
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
CLOSE='ASSISTANT_SELF_TEST_CLOSE_REQUEST normal frame closure\n'
SUCCESS='ASSISTANT_PROVIDER_TEST_PASS current-document colours native-objects undo redo async-heartbeat=16\n'
STALE='ASSISTANT_PROVIDER_TEST_PASS stale-reply no-generated-mutation async-heartbeat=16\n'
CRITICAL="(XaraLX:1): Gtk-CRITICAL **: gtk_window_move: assertion 'GTK_IS_WINDOW (window)' failed\n"
class Tests(unittest.TestCase):
 def test_clean_both_modes(self):
  for mode,line in [('success',SUCCESS),('stale',STALE)]:self.assertTrue(m.classify_run(mode,line+CLOSE,0)['passed'])
 def test_warnings_separate(self):
  log='Gtk-WARNING **: negative dimensions (GtkNotebook)\n'+SUCCESS+CLOSE
  r=m.classify_run('success',log,0);self.assertTrue(r['passed']);self.assertEqual(len(r['gtk_warnings']),1)
 def test_exact_host_failure(self):
  for mode,line in [('success',SUCCESS),('stale',STALE)]:
   r=m.classify_run(mode,line+CLOSE+CRITICAL,0)
   self.assertTrue(r['functional_passed']);self.assertTrue(r['process_exited_normally']);self.assertFalse(r['lifecycle_passed']);self.assertFalse(r['passed']);self.assertEqual(len(r['gtk_criticals']),1);self.assertEqual(len(r['assertions']),1)
 def test_critical_without_assertion(self):
  self.assertFalse(m.classify_run('success',SUCCESS+CLOSE+'GLib-GObject-CRITICAL **: error\n',0)['passed'])
 def test_assertion_without_critical(self):
  for text in ('wx: assert "x" failed\n','XARA_ASSERT_BACKTRACE_BEGIN\n'):
   self.assertFalse(m.classify_run('success',SUCCESS+CLOSE+text,0)['passed'])
 def test_crash_and_exit(self):
  for text,code in [('Segmentation fault\n',0),('',139),('',124)]:
   self.assertFalse(m.classify_run('success',SUCCESS+CLOSE+text,code)['passed'])
 def test_missing_duplicate_wrong_markers(self):
  for text in (CLOSE,SUCCESS+SUCCESS+CLOSE,STALE+CLOSE,SUCCESS+STALE+CLOSE):
   self.assertFalse(m.classify_run('success',text,0)['passed'])
 def test_missing_close_or_explicit_failure(self):
  for text in (SUCCESS,SUCCESS+CLOSE+'ASSISTANT_PROVIDER_TEST_FAIL stale\n',SUCCESS+CLOSE+'ASSISTANT_SELF_TEST_FAIL close vetoed\n'):
   self.assertFalse(m.classify_run('success',text,0)['passed'])
 def test_heartbeat(self):
  self.assertFalse(m.classify_run('success',SUCCESS.replace('=16','=0')+CLOSE,0)['passed'])
 def test_http_contract(self):
  r={'path':'/api/chat','model':'mock-model','stream':False}
  self.assertTrue(m.classify_http([r,r]))
  for rs in ([r],[],[r,dict(r,stream=True)],[r,dict(r,path='/other')]):self.assertFalse(m.classify_http(rs))
if __name__=='__main__':unittest.main()
