#!/usr/bin/env python3
"""Real official-SDK stdio round trips with a test-double editor, not GUI tests."""
import asyncio
import json
import os
from pathlib import Path
import sys
import tempfile
from datetime import timedelta
import unittest
sys.dont_write_bytecode = True
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
ROOT = Path(__file__).resolve().parents[2]


def payload(result):
    return result.structuredContent or json.loads(result.content[0].text)


class MCPProtocolTests(unittest.IsolatedAsyncioTestCase):
    async def test_stdio_handshake_schema_tools_scene_and_busy(self):
        with tempfile.TemporaryDirectory(prefix='xara-mcp-protocol.') as tmp:
            release=Path(tmp)/'release'
            env={'XARA_MCP_TEST_RELEASE':str(release)}
            parameters=StdioServerParameters(command=sys.executable,
                args=[str(ROOT/'automation/mcp/server.py'),'--binary',str(ROOT/'tests/native/fake_xara_automation.py')],env=env)
            try:
                async with stdio_client(parameters) as (read,write):
                    async with ClientSession(read,write,read_timeout_seconds=timedelta(seconds=8)) as session:
                        initialized=await session.initialize()
                        self.assertEqual(initialized.serverInfo.name,'xara-vengeance')
                        tools=await session.list_tools()
                        self.assertEqual({t.name for t in tools.tools}, {'xara_create_scene','xara_get_capabilities','xara_get_last_result'})
                        caps=await session.call_tool('xara_get_capabilities',{})
                        self.assertFalse(caps.isError);self.assertEqual(payload(caps)['transport'],'stdio')
                        self.assertEqual(payload(await session.call_tool('xara_get_last_result',{}))['status'],'no_session')
                        # Reject path/shell/unsupported operations at the protocol/schema boundary.
                        for arguments in [
                            {'scene':{'requests':[{'method':'document.save','params':{'path':'/etc/passwd'}}]}},
                            {'scene':{'requests':[]}},
                            {'scene':{'requests':[{'method':'object.create_line','params':{'x':0,'y':0,'x2':1,'y2':1,'command':'uname'}}]}},
                        ]:
                            self.assertTrue((await session.call_tool('xara_create_scene',arguments)).isError)
                        self.assertTrue((await session.call_tool('xara_get_last_result',{'path':'/etc/passwd'})).isError)
                        self.assertEqual(payload(await session.call_tool('xara_get_last_result',{}))['status'],'no_session')
                        scene=json.loads((ROOT/'tests/native/fixtures/automation-scene.json').read_text())
                        result=await session.call_tool('xara_create_scene',{'scene':scene})
                        self.assertFalse(result.isError)
                        model=payload(result)
                        self.assertEqual(model['status'],'running');self.assertTrue(model['model_verified'])
                        self.assertEqual(len(model['results'][-1]['objects']),4)
                        self.assertTrue((await session.call_tool('xara_create_scene',{'scene':scene})).isError)
                        release.touch()
                        for _ in range(100):
                            final=payload(await session.call_tool('xara_get_last_result',{}))
                            if final['status']=='completed': break
                            await asyncio.sleep(0.05)
                        self.assertEqual(final['status'],'completed');self.assertEqual(final['application_exit'],0)
                        self.assertEqual(final['helper_exit'],0)
            finally:
                release.touch()  # Test double only; never terminates a real editor.

    async def test_disconnect_keeps_test_editor_alive(self):
        with tempfile.TemporaryDirectory(prefix='xara-mcp-disconnect.') as tmp:
            release=Path(tmp)/'release'
            parameters=StdioServerParameters(command=sys.executable,
                args=[str(ROOT/'automation/mcp/server.py'),'--binary',str(ROOT/'tests/native/fake_xara_automation.py')],
                env={'XARA_MCP_TEST_RELEASE':str(release)})
            evidence=None
            try:
                async with stdio_client(parameters) as (read,write):
                    async with ClientSession(read,write,read_timeout_seconds=timedelta(seconds=8)) as session:
                        await session.initialize()
                        scene=json.loads((ROOT/'tests/native/fixtures/automation-circle.json').read_text())
                        result=await session.call_tool('xara_create_scene',{'scene':scene})
                        self.assertFalse(result.isError)
                        self.assertEqual(payload(result)['status'],'running')
                        evidence=Path(payload(result)['evidence_directory'])
                # The helper/test-editor must survive the server's normal stdio shutdown.
                self.assertFalse((evidence/'status.json').exists())
                release.touch()
                for _ in range(100):
                    if (evidence/'status.json').exists():break
                    await asyncio.sleep(0.05)
                self.assertEqual(json.loads((evidence/'status.json').read_text())['application_exit'],0)
            finally:
                release.touch()

if __name__ == '__main__': unittest.main()
