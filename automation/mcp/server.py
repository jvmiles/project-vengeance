#!/usr/bin/env python3
"""Official MCP SDK stdio server. stdout is reserved exclusively for MCP."""
import argparse
import asyncio
import json
import logging
from pathlib import Path
import sys

sys.dont_write_bytecode = True
from mcp.server import Server
from mcp.server.stdio import stdio_server
from mcp.types import CallToolResult, TextContent, Tool, ToolAnnotations
from backend import ROOT, SCHEMA, SceneManager, ToolFailure


def make_server(manager):
    server = Server('xara-vengeance', version='0.1.0')

    @server.list_tools()
    async def list_tools():
        read = ToolAnnotations(readOnlyHint=True, destructiveHint=False, openWorldHint=False)
        return [
            Tool(name='xara_create_scene', description=(
                'Create native editable objects in a NEW Xara document. Does not edit existing sessions. '
                'The human closes this editor normally before another scene. Returns startup/model result '
                'without waiting for GUI closure; query xara_get_last_result afterward.'),
                inputSchema={'type': 'object', 'required': ['scene'], 'additionalProperties': False,
                             'properties': {'scene': SCHEMA}},
                annotations=ToolAnnotations(readOnlyHint=False, destructiveHint=False,
                                            idempotentHint=False, openWorldHint=False)),
            Tool(name='xara_get_capabilities', description='Get supported automation schema and session limitations.',
                 inputSchema={'type': 'object', 'additionalProperties': False}, annotations=read),
            Tool(name='xara_get_last_result', description='Read the latest session result owned by this server; no path parameter.',
                 inputSchema={'type': 'object', 'additionalProperties': False}, annotations=read),
        ]

    @server.call_tool()
    async def call_tool(name, arguments):
        try:
            arguments = arguments or {}
            if name == 'xara_create_scene':
                if set(arguments) != {'scene'}:
                    raise ToolFailure('Expected scene only')
                result = await manager.create_scene(arguments['scene'])
            elif name == 'xara_get_capabilities':
                if arguments:
                    raise ToolFailure('Tool takes no arguments')
                result = manager.capabilities()
            elif name == 'xara_get_last_result':
                if arguments:
                    raise ToolFailure('Tool takes no arguments')
                result = manager.refresh()
            else:
                raise ToolFailure('Unknown tool')
            error = result.get('status') in ('failed', 'verification_failed')
        except (ToolFailure, OSError):
            # Avoid returning environment, file contents or provider credentials on errors.
            result = {'status': 'error', 'message': 'Request rejected or local editor unavailable; check capabilities and close any active test session'}
            error = True
        return CallToolResult(content=[TextContent(type='text', text=json.dumps(result))],
                              structuredContent=result, isError=error)
    return server


async def scheduler_wakeups():
    # SDK stdio uses worker-thread I/O. Restricted sandboxes can delay the loop's
    # thread wakeup notification; a timer guarantees bounded progress without
    # modifying protocol handling, reading client data or opening a listener.
    while True:
        await asyncio.sleep(0.1)


async def run(binary):
    manager = SceneManager(binary)
    server = make_server(manager)
    wakeups = asyncio.create_task(scheduler_wakeups())
    try:
        async with stdio_server() as (read, write):
            await server.run(read, write, server.create_initialization_options())
    finally:
        wakeups.cancel()
        await asyncio.gather(wakeups, return_exceptions=True)
    # No termination of helper/editor on disconnect: protect any unsaved user edits.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/automation-v02/XaraLX',
                        help='Operator-configured executable, never supplied by an MCP tool')
    args = parser.parse_args()
    logging.basicConfig(level=logging.WARNING, stream=sys.stderr)
    asyncio.run(run(args.binary))


if __name__ == '__main__':
    main()
