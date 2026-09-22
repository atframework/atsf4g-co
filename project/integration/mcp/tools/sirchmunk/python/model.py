"""Pinned, shared embedding cache. No LLM credentials or repository reads."""
import json
import os
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from support import atomic_json, exclusive_file, LockBusy

PIN = json.loads((Path(__file__).parent.parent / "upstream-lock.json").read_text(encoding="utf-8"))["embedding"]


def configure_cache(directory):
    directory = Path(directory).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    os.environ.update({
        "HF_HOME": str(directory / "huggingface"), "HF_HUB_CACHE": str(directory / "huggingface/hub"),
        "MODELSCOPE_CACHE": str(directory / "modelscope"), "TORCH_HOME": str(directory / "torch"),
        "XDG_CACHE_HOME": str(directory / "cache"), "HF_HUB_DISABLE_TELEMETRY": "1",
        "HF_HUB_DISABLE_IMPLICIT_TOKEN": "1", "DO_NOT_TRACK": "1", "TOKENIZERS_PARALLELISM": "false",
    })
    return directory


def download_snapshot(directory, offline=False):
    directory = configure_cache(directory)
    from huggingface_hub import snapshot_download
    # Installer worker and MCP instances share an OS lock. A killed worker does
    # not leave a permanent lock; partial Hub downloads resume on the next run.
    while True:
        try:
            with exclusive_file(directory / ".download.lock"):
                atomic_json(directory / "download-state.json", {"state": "loading", "pid": os.getpid(), **PIN})
                snapshot = snapshot_download(
                    repo_id=PIN["model"], revision=PIN["revision"],
                    cache_dir=str(directory / "huggingface/hub"), local_files_only=offline,
                    endpoint="https://huggingface.co", token=False,
                    allow_patterns=["*.json", "*.safetensors", "*.model", "*.txt"],
                    ignore_patterns=["onnx/*", "openvino/*"],
                )
                atomic_json(directory / "download-state.json", {"state": "downloaded", "snapshot": snapshot, **PIN})
                return snapshot
        except LockBusy:
            time.sleep(0.25)
        except Exception as error:
            atomic_json(directory / "download-state.json", {"state": "failed", "error": str(error), **PIN})
            raise


def warm_model(directory, offline=False):
    directory = configure_cache(directory)
    snapshot = download_snapshot(directory, offline)
    from sentence_transformers import SentenceTransformer
    model = SentenceTransformer(snapshot, device="cpu", local_files_only=True, trust_remote_code=False)
    vector = model.encode(["workspace MCP embedding check"], show_progress_bar=False)
    if len(vector[0]) != 384:
        raise RuntimeError("Unexpected embedding dimension")
    atomic_json(directory / "download-state.json", {"state": "ready", "snapshot": snapshot, **PIN})


if __name__ == "__main__":
    try:
        with exclusive_file(Path(sys.argv[1]) / '.warm.lock'):
            warm_model(sys.argv[1], "--offline" in sys.argv[2:])
    except LockBusy:
        pass  # An existing installer worker owns this job.
    except Exception as error:
        directory = configure_cache(sys.argv[1])
        atomic_json(directory / "download-state.json", {"state": "failed", "error": str(error), **PIN})
        raise
