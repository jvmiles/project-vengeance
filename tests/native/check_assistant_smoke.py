#!/usr/bin/env python3
"""Regression for model-PASS/shutdown-timeout discrepancy and real error detection."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('assistant_log', ROOT/'scripts/check-assistant-log.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
PASS = 'ASSISTANT_SELF_TEST_PASS gui-thread scene/model/rejection/current-document/undo/redo\n'


class SmokeClassifierTests(unittest.TestCase):
    def test_actual_timeout_evidence_is_not_model_failure(self):
        d=ROOT/'docs/diagnostics/ai-assistant-0.1/timeout-regression'
        result=MODULE.classify((d/'startup.log').read_text(),json.loads((d/'status.json').read_text())['application_wrapper_exit'])
        self.assertTrue(result['model_checks_passed'])
        self.assertFalse(result['lifecycle_completed'])
        self.assertFalse(result['smoke_passed'])
        self.assertEqual(result['failure_reasons'],['40-second process timeout; native lifecycle did not complete'])
        self.assertTrue(any('negative dimensions' in w for w in result['gtk_warnings']))

    def test_clean_exit_with_model_pass(self):
        self.assertTrue(MODULE.classify(PASS,0)['smoke_passed'])

    def test_warnings_retained_but_not_confused_with_assertions(self):
        result=MODULE.classify(PASS+'Gtk-WARNING: Drawing a gadget with negative dimensions (owner GtkNotebook)\n',0)
        self.assertTrue(result['smoke_passed'])
        self.assertEqual(len(result['gtk_warnings']),1)

    def test_success_marker_never_overrides_nonzero_exit(self):
        for code in (1,7,124,137,139):
            with self.subTest(code=code):self.assertFalse(MODULE.classify(PASS,code)['smoke_passed'])

    def test_wx_assertions_and_gtk_critical_fail_even_with_zero_exit(self):
        for message in ('assert "cr" failed','XARA_ASSERT_BACKTRACE_BEGIN','Gtk-CRITICAL: unexpected bad widget','GLib-GObject-CRITICAL: invalid object','Segmentation fault'):
            with self.subTest(message=message):self.assertFalse(MODULE.classify(PASS+message,0)['smoke_passed'])

    def test_no_duplicate_or_conflicting_pass(self):
        for log in ('',PASS+PASS,PASS+'ASSISTANT_SELF_TEST_FAIL close vetoed\n'):
            with self.subTest(log=log):self.assertFalse(MODULE.classify(log,0)['smoke_passed'])

    def test_real_desktop_shutdown_critical_is_not_hidden_by_exit_zero(self):
        log=(ROOT/'docs/diagnostics/ai-assistant-0.1/desktop-verified/startup.log').read_text()
        result=MODULE.classify(PASS+log,0)
        self.assertFalse(result['smoke_passed'])
        self.assertTrue(any('assertion' in r for r in result['failure_reasons']))

    def test_cli_records_explicit_failure_reasons(self):
        with tempfile.TemporaryDirectory(prefix='xara-assistant-classifier.') as tmp:
            log=Path(tmp)/'startup.log';status=Path(tmp)/'status.json';log.write_text(PASS)
            call=subprocess.run([sys.executable,str(ROOT/'scripts/check-assistant-log.py'),str(log),'124','--status',str(status)],capture_output=True,text=True)
            self.assertEqual(call.returncode,1)
            self.assertIn('timeout',call.stdout)
            self.assertIn('Model checks passed',call.stdout)
            self.assertFalse(json.loads(status.read_text())['smoke_passed'])


if __name__=='__main__':unittest.main()
