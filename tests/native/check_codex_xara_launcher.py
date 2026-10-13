#!/usr/bin/env python3
"""Check launcher boundary failures; no model calls, MCP startup or native launch."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('launcher',ROOT/'scripts/codex-xara.py')
launcher=importlib.util.module_from_spec(spec);spec.loader.exec_module(launcher)

class LauncherTests(unittest.TestCase):
    def test_dry_run_never_starts_process(self):
        output=io.StringIO()
        with patch.object(launcher.shutil,'which',return_value='/known/codex'), patch.object(launcher.subprocess,'run') as start, contextlib.redirect_stdout(output):
            self.assertEqual(launcher.main(['--dry-run']),0)
            start.assert_not_called()
        result=json.loads(output.getvalue())
        self.assertFalse(result['writes_global_config']);self.assertFalse(result['launches_model'])
        self.assertIn('xara_create_scene',result['argv'][-1])
        self.assertIn('-s',result['argv']);self.assertIn('read-only',result['argv'])
        self.assertIn('required=true',result['argv'][-2])

    def test_blocked_display_never_starts_codex(self):
        def fake_run(argv,**kwargs):
            self.assertFalse(kwargs['shell'])
            if argv[0]=='/known/xdpyinfo':return subprocess.CompletedProcess(argv,1,stdout='',stderr='blocked')
            self.assertEqual(argv[0],str(ROOT/'build/mcp-venv/bin/python'))
            return subprocess.CompletedProcess(argv,0,stdout=b'',stderr=b'')
        original_is_file=Path.is_file
        prerequisites={ROOT/'build/mcp-venv/bin/python',ROOT/'build/automation-v02/XaraLX'}
        def is_file(path):return True if path in prerequisites else original_is_file(path)
        with patch.object(Path,'is_file',is_file), patch.object(launcher.os,'access',return_value=True), patch.object(launcher.shutil,'which',side_effect=lambda n:'/known/'+n), patch.object(launcher.subprocess,'run',side_effect=fake_run) as start, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(launcher.main([]),1)
            self.assertEqual(start.call_count,2)
            self.assertTrue(all(c.args[0][0]!='/known/codex' for c in start.call_args_list))

    def test_missing_prerequisites_never_starts_process(self):
        with patch.object(Path,'is_file',return_value=False), patch.object(launcher.shutil,'which',return_value='/known/codex'), patch.object(launcher.subprocess,'run') as start, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(launcher.main([]),1)
            start.assert_not_called()

    def test_config_check_uses_fixed_argv_no_model(self):
        with patch.object(launcher.shutil,'which',return_value='/known/codex'), patch.object(launcher.subprocess,'run',return_value=subprocess.CompletedProcess([],0)) as start:
            self.assertEqual(launcher.main(['--check-config']),0)
        argv=start.call_args.args[0]
        self.assertEqual(argv[-4:],['mcp','get','xara_vengeance_test','--json'])
        self.assertFalse(start.call_args.kwargs['shell'])
        self.assertNotIn('mcp add',argv)
        value=launcher.settings(':0.0','/tmp/a"$(false).authority')
        self.assertIn('\\"$(false)',value) # Literal TOML string, never shell-evaluated.

if __name__=='__main__':unittest.main()
