"""SDK-independent boundary tests; real package smoke is recorded separately."""
import concurrent.futures
import asyncio
import contextlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import types
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'python'))
# The executable bridge protects stdout on import. Restore it for unittest.
with contextlib.redirect_stdout(sys.stdout):
    from bridge import Engine
from model import PIN, download_snapshot
from support import exclusive_file, LockBusy
from bootstrap import check_native


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.engine = Engine(self.root, self.root / 'build/state/work', self.root / 'build/models',
                             {'baseURL': 'https://example.test/v1', 'apiKey': 'test', 'model': 'test'}, True)

    def test_preprocessor_probe_checks_content_without_version_flag_or_global_cache(self):
        def execute(argv, **options):
            self.assertNotIn('--version', argv)
            self.assertIn('--rga-cache-path=' + str(self.root / 'probe/cache'), argv)
            self.assertTrue(Path(argv[-1]).is_relative_to(self.root))
            return types.SimpleNamespace(returncode=0, stdout=b'workspace-mcp-probe\n')
        with patch('bootstrap.subprocess.run', execute):
            self.assertTrue(check_native('rga-preproc', 'rga-preproc', self.root / 'probe'))
        with patch('bootstrap.subprocess.run', return_value=types.SimpleNamespace(returncode=0, stdout=b'')):
            self.assertFalse(check_native('rga-preproc', 'rga-preproc', self.root / 'probe'))

    def test_protocol_reader_preserves_frames_and_observes_eof(self):
        code = "import sys,json; sys.path.insert(0,sys.argv[1]); from bridge import protocol_lines; sys.stdout=sys.__stdout__; print(json.dumps([line.decode() for line in protocol_lines()]))"
        result = subprocess.run([sys.executable, '-I', '-c', code, str(HERE.parent / 'python')],
                                input=b'{"id":1}\n{"id":2}\n', capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), ['{"id":1}\n', '{"id":2}\n'])

    def test_search_persists_before_reply_and_lists_through_bounded_storage_api(self):
        calls = []
        async def search(**options):
            calls.append('search')
            return ['example.cpp']
        async def find(query, limit):
            calls.append((query, limit))
            return [{'id': 'saved'}]
        storage = types.SimpleNamespace(force_sync=lambda: calls.append('sync'), find=find, close=lambda: calls.append('close'))
        self.engine.searcher = types.SimpleNamespace(search=search, knowledge_storage=storage)
        self.assertEqual(asyncio.run(self.engine.call('search', {'query': 'example', 'mode': 'FILENAME_ONLY'})), ['example.cpp'])
        self.assertEqual(calls, ['search', 'sync'])
        self.assertEqual(asyncio.run(self.engine.call('list_clusters', {'limit': 2})), [{'id': 'saved'}])
        self.assertEqual(calls[-1], ('', 2))
        asyncio.run(self.engine.call('shutdown', {}))
        self.assertEqual(calls[-1], 'close')

    def test_evolution_waits_for_model_load_and_warmup(self):
        future = concurrent.futures.Future()
        ready = [False]
        embedding = types.SimpleNamespace(is_ready=lambda: ready[0], _model_future=future)
        searcher = types.SimpleNamespace(embedding_client=None, knowledge_evolver=None, knowledge_storage=object())
        self.engine.embedding, self.engine.searcher, self.engine.llm = embedding, searcher, object()
        self.engine.snapshot = str(self.root / 'downloaded-snapshot')
        with patch.dict(sys.modules, {'sirchmunk.search': types.SimpleNamespace(KnowledgeEvolver=lambda **args: args)}):
            self.engine.refresh()
            self.assertFalse(self.engine.status()['enable_knowledge_evolution'])
            self.assertIsNone(searcher.knowledge_evolver)
            ready[0] = True
            future.set_result(object())
            self.engine.refresh()
            self.assertTrue(self.engine.status()['enable_knowledge_evolution'])
            self.assertIs(searcher.knowledge_evolver['embedding'], embedding)
            self.assertIs(searcher.knowledge_evolver['knowledge_storage'], searcher.knowledge_storage)
            self.assertEqual(json.loads((self.engine.model_dir / 'download-state.json').read_text())['state'], 'ready')

    def test_model_failure_keeps_basic_search_available(self):
        future = concurrent.futures.Future()
        future.set_exception(RuntimeError('download failed'))
        self.engine.embedding = types.SimpleNamespace(is_ready=lambda: False, _model_future=future)
        self.engine.searcher = object()
        self.engine.refresh()
        self.assertEqual(self.engine.status()['state'], 'ready')
        self.assertEqual(self.engine.status()['embedding'], 'failed')
        self.assertFalse(self.engine.status()['enable_knowledge_evolution'])

    def test_scope_cannot_escape_workspace(self):
        (self.root / 'Source').mkdir()
        self.assertEqual(self.engine.scopes(['Source']), [str(self.root / 'Source')])
        for value in (['../outside'], [str(self.root)], ['missing'], 'Source'):
            with self.assertRaises((ValueError, FileNotFoundError)):
                self.engine.scopes(value)

    def test_offline_model_never_falls_back_to_network_and_permission_errors_do_not_retry(self):
        calls = []
        def missing(**args):
            calls.append(args)
            raise PermissionError('cache not writable')
        with patch.dict(sys.modules, {'huggingface_hub': types.SimpleNamespace(snapshot_download=missing)}):
            with self.assertRaises(PermissionError):
                download_snapshot(self.root / 'model', offline=True)
        self.assertEqual(len(calls), 1)
        self.assertTrue(calls[0]['local_files_only'])
        self.assertEqual(calls[0]['revision'], PIN['revision'])
        self.assertEqual(json.loads((self.root / 'model/download-state.json').read_text())['state'], 'failed')

    def test_download_lock_is_exclusive_and_releases(self):
        file = self.root / 'model.lock'
        with exclusive_file(file):
            with self.assertRaises(LockBusy):
                with exclusive_file(file):
                    self.fail('second owner acquired lock')
        with exclusive_file(file):
            pass

    def test_eof_and_forced_exit_kill_native_descendants(self):
        code = "import sys,subprocess; sys.path.insert(0,sys.argv[1]); from lifecycle import contain_process_tree,terminate_tree; contain_process_tree(); child=subprocess.Popen([sys.executable,'-I','-c','import time; time.sleep(60)']); print(child.pid,flush=True); sys.stdin.read(); terminate_tree()"
        for forced in ((False, True) if os.name == 'nt' else (False,)):
            process = subprocess.Popen([sys.executable, '-I', '-c', code, str(HERE.parent / 'python')], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                line = process.stdout.readline()
                self.assertTrue(line.strip().isdigit(), process.stderr.read() if process.poll() is not None else line)
                pid = int(line)
                if os.name == 'nt':
                    import ctypes
                    from ctypes import wintypes
                    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
                    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
                    kernel.OpenProcess.restype = wintypes.HANDLE
                    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
                    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
                    handle = kernel.OpenProcess(0x100000, False, pid)
                    self.assertTrue(handle)
                if forced:
                    process.kill()
                else:
                    process.stdin.close()
                process.wait(timeout=5)
                if os.name == 'nt':
                    try:
                        self.assertEqual(kernel.WaitForSingleObject(handle, 5000), 0)
                    finally:
                        kernel.CloseHandle(handle)
                else:
                    deadline = time.monotonic() + 5
                    while time.monotonic() < deadline:
                        try:
                            os.kill(pid, 0)
                            stat = Path(f'/proc/{pid}/stat')
                            if stat.exists() and stat.read_text().split(') ')[1].startswith('Z'):
                                break
                        except ProcessLookupError:
                            break
                        time.sleep(0.05)
                    else:
                        self.fail('native descendant outlived EOF')
            finally:
                if process.poll() is None:
                    process.kill()
                process.wait()
                for stream in (process.stdin, process.stdout, process.stderr):
                    stream.close()


if __name__ == '__main__':
    unittest.main()
