#!/usr/bin/env python3
"""Backend validation tests without MCP SDK or native GUI execution."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('xara_mcp_backend', ROOT / 'automation/mcp/backend.py')
backend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)

class BackendTests(unittest.IsolatedAsyncioTestCase):
    def scene(self):
        return json.loads((ROOT / 'tests/native/fixtures/automation-scene.json').read_text())

    def test_schema_and_circle(self):
        self.assertEqual(len(backend.validate_scene(self.scene())), 6)
        circle = json.loads((ROOT / 'tests/native/fixtures/automation-circle.json').read_text())
        self.assertEqual(len(backend.validate_scene(circle)), 3)

    def test_no_paths_code_or_unknown_operations(self):
        for value in [
            {'requests': [{'method': 'document.save', 'params': {'path': '/tmp/owned.xar'}}]},
            {'requests': [{'method': 'object.create_text', 'params': {'x':0,'y':0,'font_size':24000,'text':'hi','command':'rm -rf /'}}]},
            {'requests': [{'method': 'document.list_objects', 'params': {}}]},
            dict(self.scene(), binary='/bin/sh'),
            dict(self.scene(), requests=[self.scene()['requests'][1]] * 17),
        ]:
            with self.subTest(value=value), self.assertRaises(backend.ToolFailure):
                backend.validate_scene(value)

    def test_transaction_semantics_and_limits(self):
        value = self.scene(); value['requests'].insert(2, {'method':'document.list_objects','params':{}})
        with self.assertRaises(backend.ToolFailure): backend.validate_scene(value)
        value = self.scene(); value['requests'][1]['params']['x'] = 10000000
        with self.assertRaises(backend.ToolFailure): backend.validate_scene(value)
        value = self.scene(); value['requests'][-2]['params']['text'] = 'a' * 65536
        with self.assertRaises(backend.ToolFailure): backend.validate_scene(value)

    async def test_rejection_never_launches(self):
        manager = backend.SceneManager(ROOT / 'build/automation-v02/XaraLX', startup_wait=0)
        with patch.object(backend.subprocess, 'Popen') as launch:
            with self.assertRaises(backend.ToolFailure):
                await manager.create_scene({'requests':[]})
            launch.assert_not_called()
        self.assertEqual(manager.refresh()['status'], 'no_session')

    async def test_busy_never_launches_or_kills(self):
        manager = backend.SceneManager(ROOT / 'build/automation-v02/XaraLX', startup_wait=0)
        class Active:
            def poll(self): return None
            def kill(self): raise AssertionError('must not kill GUI')
        manager.process = Active()
        with patch.object(backend.subprocess, 'Popen') as launch:
            with self.assertRaises(backend.ToolFailure): await manager.create_scene(self.scene())
            launch.assert_not_called()

    def test_result_reader_blocks_symlinks_and_public_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'result.json'; path.write_text('{}'); path.chmod(0o600)
            self.assertEqual(backend.read_private_json(path), {})
            link = Path(tmp) / 'link.json'; link.symlink_to(path)
            with self.assertRaises(OSError): backend.read_private_json(link)
            path.chmod(0o644)
            with self.assertRaises(backend.ToolFailure): backend.read_private_json(path)

if __name__ == '__main__': unittest.main()
