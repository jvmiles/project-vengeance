#!/usr/bin/env python3
"""Operator-run official-SDK client; human verification required for native scene."""
import argparse
import asyncio
from datetime import timedelta
import json
import os
from pathlib import Path
import sys
import tempfile
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
ROOT = Path(__file__).resolve().parents[1]


def payload(result):
    return result.structuredContent or json.loads(result.content[0].text)


async def wakeups():
    while True: await asyncio.sleep(0.1)


async def smoke(args):
    evidence = Path(tempfile.mkdtemp(prefix='xara-mcp-client.'))
    print('MCP client evidence:', evidence, flush=True)
    environment = {k:v for k,v in os.environ.items() if k in
                   ('DISPLAY','XAUTHORITY','XDG_RUNTIME_DIR','XDG_SESSION_TYPE','LANG','LC_ALL','PATH')}
    parameters = StdioServerParameters(command=sys.executable,
        args=[str(ROOT/'automation/mcp/server.py'),'--binary',str(args.binary.resolve())],
        env=environment, cwd=str(ROOT))
    history = {}
    timer = asyncio.create_task(wakeups())
    try:
        async with stdio_client(parameters) as (read,write):
            async with ClientSession(read,write,read_timeout_seconds=timedelta(seconds=30)) as client:
                initialized = await client.initialize()
                history['initialize'] = initialized.model_dump(mode='json')
                tools = await client.list_tools()
                history['tools'] = [t.name for t in tools.tools]
                print('MCP tools:', ', '.join(history['tools']), flush=True)
                capabilities = await client.call_tool('xara_get_capabilities',{})
                if capabilities.isError: raise RuntimeError('Capabilities request failed')
                history['capabilities'] = payload(capabilities)
                if args.no_launch:
                    (evidence/'protocol.json').write_text(json.dumps(history,indent=2)+'\n')
                    print('PASS: official SDK initialization and discovery; editor not launched.')
                    return 0
                scene = json.loads(args.scene.read_text())
                response = await client.call_tool('xara_create_scene',{'scene':scene})
                history['create_scene'] = payload(response)
                (evidence/'protocol.json').write_text(json.dumps(history,indent=2)+'\n')
                if response.isError: raise RuntimeError('Scene rejected; inspect protocol.json')
                print('Scene status:',history['create_scene']['status'],flush=True)
                if 'evidence_directory' in history['create_scene']:
                    print('Editor evidence:',history['create_scene']['evidence_directory'],flush=True)
                print('Check four editable objects, colours, text, one Undo and one Redo. Close Xara normally.',flush=True)
                await asyncio.to_thread(input, 'After normal closure, press Enter here: ')
                for _ in range(100):
                    last = await client.call_tool('xara_get_last_result',{})
                    history['last_result'] = payload(last)
                    if history['last_result']['status'] in ('completed','failed','verification_failed'): break
                    await asyncio.sleep(0.1)
                (evidence/'protocol.json').write_text(json.dumps(history,indent=2)+'\n')
                print('Final status:',history['last_result']['status'],flush=True)
                if last.isError or history['last_result']['status']!='completed':
                    print('NOT VERIFIED: inspect evidence; do not force-close an editor with unsaved work.')
                    return 1
                print('PASS: MCP scene model checks and normal application exit. GUI/Undo/Redo are human checks.')
                return 0
    finally:
        timer.cancel()
        await asyncio.gather(timer, return_exceptions=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,default=ROOT/'build/automation-v02/XaraLX')
    parser.add_argument('--scene',type=Path,default=ROOT/'tests/native/fixtures/automation-scene.json')
    parser.add_argument('--no-launch',action='store_true')
    args=parser.parse_args()
    return asyncio.run(smoke(args))

if __name__=='__main__': raise SystemExit(main())
