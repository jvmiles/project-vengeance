"""One startup request per new editor. No sockets, shell evaluation or user file paths."""
import asyncio
import importlib.util
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('xara_startup_api', ROOT / 'scripts/automation-poc.py')
api = importlib.util.module_from_spec(spec)
spec.loader.exec_module(api)
SCHEMA = json.loads((ROOT / 'automation/request.schema.json').read_text())
VALIDATOR = Draft202012Validator(SCHEMA)
MAX_REQUEST = 65536
MAX_RESULT = 4 * 1024 * 1024

class ToolFailure(ValueError):
    pass


def validate_scene(scene):
    try:
        encoded = json.dumps(scene, ensure_ascii=True, allow_nan=False).encode()
        if len(encoded) > MAX_REQUEST:
            raise ToolFailure('Scene exceeds 64 KiB')
        if not VALIDATOR.is_valid(scene):
            raise ToolFailure('Scene does not match automation/request.schema.json')
        requests = api.validate(scene)
        if not any(r['method'].startswith('object.create_') for r in requests):
            raise ToolFailure('Scene must contain at least one creation')
        return requests
    except (TypeError, ValueError, OverflowError, RecursionError) as error:
        if isinstance(error, ToolFailure):
            raise
        raise ToolFailure('Invalid scene parameters') from None


def read_private_json(path):
    # Only server-owned session paths call this; never accept a path from a tool.
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077 or info.st_size > MAX_RESULT:
            raise ToolFailure('Invalid session result file')
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            data = stream.read(MAX_RESULT + 1)
        if len(data) > MAX_RESULT:
            raise ToolFailure('Session result exceeds limit')
        return json.loads(data, object_pairs_hook=api.unique_keys)
    finally:
        os.close(fd)


class SceneManager:
    def __init__(self, binary, startup_wait=15.0):
        self.binary = Path(binary).resolve()
        self.startup_wait = startup_wait
        self.root = Path(tempfile.mkdtemp(prefix='xara-mcp.'))
        self.lock = asyncio.Lock()
        self.process = None
        self.session_dir = None
        self.requests = None
        self.counter = 0
        self.last = None

    def capabilities(self):
        return {
            'mcp_version': '0.1', 'automation_version': '0.2', 'transport': 'stdio',
            'session_model': 'one_request_per_new_editor', 'persistent_connection': False,
            'tools': ['xara_create_scene', 'xara_get_capabilities', 'xara_get_last_result'],
            'automation_schema': SCHEMA,
            'binary_available': self.binary.is_file() and os.access(self.binary, os.X_OK),
            'max_request_bytes': MAX_REQUEST,
            'transaction': 'optional initial list, creations, optional final list',
            'limitations': [
                'Only creates in a new isolated-preference document; cannot control an existing editor',
                'Creation-time style only; ASCII single-line text; no save, export, delete or arbitrary file access',
                'Only one active editor per server; close normally before another scene',
                'Tool returns before editor closes; query last result for final exit status',
                'Undo/Redo are native editor commands, not MCP tools',
                'Cancellation or server disconnect does not close the editor',
                'Last result is limited to this server lifetime; evidence directories are retained',
            ],
        }

    def _evidence(self):
        if self.session_dir is None:
            return None
        log = self.session_dir / 'helper.log'
        if not log.exists():
            return None
        with log.open('rb') as stream:
            first = stream.readline(4096).decode('utf-8', errors='replace').strip()
        prefix = 'Automation evidence: '
        if not first.startswith(prefix):
            return None
        path = Path(first[len(prefix):])
        if path.is_symlink() or path.resolve().parent != self.session_dir or not path.name.startswith('xara-automation.'):
            raise ToolFailure('Unexpected evidence directory')
        if not path.is_dir():
            return None
        return path

    def refresh(self):
        if self.last is None:
            return {'status': 'no_session'}
        result = dict(self.last)
        if self.process is None:
            return dict(result, status='failed', editor_may_be_open=False)
        result['editor_may_be_open'] = self.process.poll() is None
        evidence = self._evidence()
        if evidence:
            result['evidence_directory'] = str(evidence)
            try:
                snapshots = read_private_json(evidence / 'result.json')
                api.verify_results(self.requests, snapshots)
                result.update(status='running', model_verified=True, results=snapshots)
            except (FileNotFoundError, json.JSONDecodeError):
                pass
            except (ValueError, KeyError, TypeError, OSError):
                result.update(status='verification_failed', model_verified=False)
            try:
                status = read_private_json(evidence / 'status.json')
                result['application_exit'] = status['application_exit']
            except (FileNotFoundError, json.JSONDecodeError):
                pass
            except (KeyError, TypeError, OSError, ValueError):
                result['status'] = 'verification_failed'
        helper_exit = self.process.poll()
        if helper_exit is not None:
            result['helper_exit'] = helper_exit
            result['editor_may_be_open'] = False
            success = helper_exit == 0 and result.get('application_exit') == 0 and result.get('model_verified') is True
            result['status'] = 'completed' if success else 'failed'
        return result

    async def create_scene(self, scene):
        requests = validate_scene(scene)  # Entire batch before creating files/processes.
        async with self.lock:
            if self.process is not None and self.process.poll() is None:
                raise ToolFailure('Editor session is active; close it normally, then request another scene')
            if not self.binary.is_file() or not os.access(self.binary, os.X_OK):
                raise ToolFailure('Configured experimental executable is unavailable')
            self.counter += 1
            self.session_dir = self.root / ('session-%04d' % self.counter)
            self.session_dir.mkdir(mode=0o700)
            request_path = self.session_dir / 'scene.json'
            with request_path.open('x', encoding='utf-8') as stream:
                json.dump(scene, stream)
            request_path.chmod(0o600)
            self.requests = requests
            self.last = {'session_id': str(self.counter), 'status': 'starting', 'model_verified': False,
                         'visual_verification': 'manual', 'undo_redo_verification': 'manual'}
            env = dict(os.environ, TMPDIR=str(self.session_dir))
            self.process = None
            with (self.session_dir / 'helper.log').open('xb') as log:
                self.process = subprocess.Popen(
                    [sys.executable, '-I', str(ROOT / 'scripts/automation-poc.py'),
                     '--binary', str(self.binary), '--request', str(request_path)],
                    stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                    env=env, cwd=str(ROOT), shell=False, umask=0o077, start_new_session=True,
                )
            # Do not await GUI shutdown in the tool: keep the MCP client responsive.
            deadline = asyncio.get_running_loop().time() + self.startup_wait
            while True:
                result = self.refresh()
                if result['status'] != 'starting' or asyncio.get_running_loop().time() >= deadline:
                    return result
                await asyncio.sleep(0.1)
