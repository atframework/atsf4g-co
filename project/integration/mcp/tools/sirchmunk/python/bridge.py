"""Private line RPC child; the JavaScript parent serves MCP over stdio.

Keep protocol stdout separate before importing any third-party package. EOF
is a lifeline even while the event loop is inside model/LLM initialization.
"""
import asyncio
import json
import os
from pathlib import Path
import queue
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from model import PIN, configure_cache, download_snapshot
from lifecycle import contain_process_tree, terminate_tree
from support import atomic_json

PROTOCOL = sys.stdout
sys.stdout = sys.stderr
MAX_FRAME = 1024 * 1024


def protocol_lines():
    if os.name != 'nt':
        while line := sys.stdin.buffer.readline(MAX_FRAME + 1):
            yield line
        return
    # Blocking stdin reads can stall NumPy's Windows native initialization.
    # Peek first, then read only available bytes so this lifeline also works
    # while native modules load and still observes a closed client promptly.
    import ctypes
    import msvcrt
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.ReadFile.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
    kernel.ReadFile.restype = wintypes.BOOL
    kernel.PeekNamedPipe.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
    kernel.PeekNamedPipe.restype = wintypes.BOOL
    handle = msvcrt.get_osfhandle(sys.stdin.fileno())
    buffer = ctypes.create_string_buffer(65536)
    size = wintypes.DWORD()
    pending = b''
    while True:
        available = wintypes.DWORD()
        if not kernel.PeekNamedPipe(handle, None, 0, None, ctypes.byref(available), None):
            error = ctypes.get_last_error()
            if error in (109, 232):  # broken/closed pipe
                if pending:
                    yield pending
                return
            raise ctypes.WinError(error)
        if not available.value:
            time.sleep(0.02)
            continue
        if not kernel.ReadFile(handle, buffer, min(len(buffer), available.value), ctypes.byref(size), None):
            raise ctypes.WinError(ctypes.get_last_error())
        if not size.value:
            return
        pending += buffer.raw[:size.value]
        while b'\n' in pending:
            line, pending = pending.split(b'\n', 1)
            yield line + b'\n'
        if len(pending) > MAX_FRAME:
            yield pending  # caller rejects oversized frames and closes the tree
            return


class Engine:
    def __init__(self, root, work, model_dir, config, offline=False):
        self.root = Path(root).resolve(strict=True)
        self.work = Path(work).resolve()
        self.model_dir = configure_cache(model_dir)
        self.config, self.offline = config, offline
        self.searcher = None
        self.embedding = None
        self.evolution = False
        self.embedding_error = None
        self.snapshot = None

    def initialize(self):
        # Never let upstream bootstrap tools into ~/.local/bin or LOCALAPPDATA.
        from sirchmunk.utils.deps import check_dependencies
        if not check_dependencies():
            raise RuntimeError("rg/rga missing; rerun setup.js")
        from sirchmunk import AgenticSearch
        from sirchmunk.llm.openai_chat import OpenAIChat
        self.llm = OpenAIChat(base_url=self.config["baseURL"], api_key=self.config["apiKey"], model=self.config["model"])
        self.searcher = AgenticSearch(llm=self.llm, work_path=self.work, paths=[str(self.root)],
                                     verbose=False, reuse_knowledge=False, enable_knowledge_evolution=False)
        from sirchmunk.utils.embedding_util import EmbeddingUtil
        engine = self

        class PinnedEmbedding(EmbeddingUtil):
            @staticmethod
            def _download_model(model_id, cache_dir=None):
                engine.snapshot = download_snapshot(engine.model_dir, engine.offline)
                return engine.snapshot

        self.embedding = PinnedEmbedding(model_id=PIN["model"], device="cpu", cache_dir=str(self.model_dir))
        self.embedding.start_loading()

    def refresh(self):
        if self.evolution or not self.embedding:
            return
        if not self.embedding.is_ready():
            future = self.embedding._model_future  # pinned 0.2.0 completion contract
            if future.done() and future.exception():
                self.embedding_error = str(future.exception())
            return
        # Same objects that AgenticSearch(enable_knowledge_evolution=True)
        # creates; attach only after download, load AND warm-up have succeeded.
        # Called between serialized requests, so storage cannot change mid-query.
        from sirchmunk.search import KnowledgeEvolver
        self.searcher.embedding_client = self.embedding
        self.searcher.knowledge_evolver = KnowledgeEvolver(
            llm=self.llm, embedding=self.embedding, knowledge_storage=self.searcher.knowledge_storage,
            work_path=self.work, log_callback=None)
        self.evolution = True
        if self.snapshot:
            atomic_json(self.model_dir / 'download-state.json', {"state": "ready", "snapshot": self.snapshot, **PIN})

    def status(self):
        return {"state": "ready" if self.searcher else "starting", "embedding": "ready" if self.evolution else "failed" if self.embedding_error else "loading",
                "enable_knowledge_evolution": self.evolution, "embedding_error": self.embedding_error, "model": PIN["model"]}

    def scopes(self, scopes):
        scopes = scopes or ["."]
        if not isinstance(scopes, list) or not scopes or len(scopes) > 16:
            raise ValueError("paths must be a list of workspace-relative paths")
        resolved = []
        for scope in scopes:
            if not isinstance(scope, str) or Path(scope).is_absolute() or ".." in Path(scope).parts:
                raise ValueError("paths must stay inside this workspace")
            target = (self.root / scope).resolve(strict=True)
            if not target.is_relative_to(self.root) or target.is_relative_to(self.work.parent):
                raise ValueError("paths must stay inside source files in this workspace")
            resolved.append(str(target))
        return resolved

    async def call(self, method, params):
        if method == "status":
            return self.status()
        if method == "shutdown":
            self.searcher.knowledge_storage.close()
            return {"stopped": True}
        if method == "search":
            query = params.get("query")
            mode = params.get("mode", "FAST")
            if not isinstance(query, str) or not query.strip() or len(query) > 8192 or mode not in ("FAST", "DEEP", "FILENAME_ONLY"):
                raise ValueError("invalid query or mode")
            excludes = ["**/.git/**", "**/node_modules/**", "**/integration/mcp/**", "**/Intermediate/**", "**/Saved/**", "**/Binaries/**", "**/DerivedDataCache/**", "**/.codegraph*/**", "**/.tgrep/**"]
            result = await self.searcher.search(query=query, paths=self.scopes(params.get("paths")), mode=mode,
                                                exclude=excludes, max_loops=5, max_token_budget=32000)
            # Persist completed searches before acknowledging them. EOF and
            # forced parent termination must not lose the last minute of data.
            self.searcher.knowledge_storage.force_sync()
            return serialize(result)
        if method == "get_cluster":
            cluster = params.get("cluster_id")
            if not isinstance(cluster, str) or not cluster or len(cluster) > 256:
                raise ValueError("invalid cluster_id")
            return serialize(await self.searcher.knowledge_storage.get(cluster))
        if method == "list_clusters":
            limit = params.get("limit", 10)
            if not isinstance(limit, int) or isinstance(limit, bool) or not 1 <= limit <= 100:
                raise ValueError("limit must be between 1 and 100")
            # 0.2.0's MCP service calls list_all(), but that method is absent
            # from its actual storage class. Empty find matches saved clusters
            # and enforces the limit in the storage query itself.
            clusters = await self.searcher.knowledge_storage.find('', limit=limit)
            return [serialize(cluster) for cluster in clusters]
        raise ValueError("unknown method")


def serialize(value):
    if hasattr(value, "model_dump"):
        return value.model_dump(mode="json")
    if hasattr(value, "to_dict"):
        return value.to_dict()
    if isinstance(value, list):
        return [serialize(item) for item in value]
    return value


async def main(args):
    config_file, root, work, model_dir = args[:4]
    inbox = queue.Queue(maxsize=8)

    def read_stdin():
        for line in protocol_lines():
            if len(line) > MAX_FRAME:
                terminate_tree(2)
            try:
                inbox.put_nowait(json.loads(line))
            except (ValueError, queue.Full):
                terminate_tree(2)
        # Includes a killed JS parent. Terminates model threads promptly.
        terminate_tree()

    threading.Thread(target=read_stdin, daemon=True).start()
    config = json.loads(Path(config_file).read_text(encoding="utf-8"))
    engine = Engine(root, work, model_dir, config, "--offline" in args[4:])
    engine.initialize()
    while True:
        engine.refresh()
        try:
            request = inbox.get_nowait()
        except queue.Empty:
            await asyncio.sleep(0.1)
            continue
        try:
            result = await engine.call(request["method"], request.get("params", {}))
            response = {"jsonrpc": "2.0", "id": request.get("id"), "result": result}
        except Exception as error:
            message = str(error).replace(config["apiKey"], "[redacted]")
            response = {"jsonrpc": "2.0", "id": request.get("id"), "error": {"code": -32602 if isinstance(error, ValueError) else -32000, "message": message}}
        text = json.dumps(response, ensure_ascii=False, default=str)
        if len(text.encode("utf-8")) > MAX_FRAME:
            text = json.dumps({"jsonrpc": "2.0", "id": request.get("id"), "error": {"code": -32000, "message": "result exceeds size limit; narrow the query"}})
        PROTOCOL.write(text + "\n")
        PROTOCOL.flush()


if __name__ == "__main__":
    contain_process_tree()
    asyncio.run(main(sys.argv[1:]))
