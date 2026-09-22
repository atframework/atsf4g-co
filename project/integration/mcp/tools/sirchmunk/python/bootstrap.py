"""Prepare a pinned Sirchmunk wheel, private venv and verified rg/rga binaries."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from support import atomic_json, exclusive_file
from lifecycle import contain_process_tree


def acquire(spec, directory, offline=False):
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / spec["filename"]
    if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() == spec["sha256"]:
        return target
    if offline:
        raise RuntimeError(f"Missing or invalid offline artifact: {target.name}")
    temporary = target.with_name(target.name + ".part")
    try:
        request = urllib.request.Request(spec["url"], headers={"User-Agent": "workspace-mcp/1"})
        with urllib.request.urlopen(request, timeout=60) as response, temporary.open("wb") as output:
            shutil.copyfileobj(response, output)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != spec["sha256"]:
            raise RuntimeError(f"SHA256 mismatch: {target.name}")
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)
    return target


def check_native(file, name, scratch):
    if name != 'rga-preproc':
        return subprocess.run([str(file), '--version'], capture_output=True, timeout=15).returncode == 0
    # rga-preproc takes a filename, not --version/--help. Exercise its built-in
    # gzip adapter without external converters or a user-global cache/config.
    scratch.mkdir(parents=True, exist_ok=True)
    fixture = scratch / 'probe.txt.gz'
    config = scratch / 'config.json'
    fixture.write_bytes(gzip.compress(b'workspace-mcp-probe\n'))
    config.write_text('{}', encoding='utf-8')
    result = subprocess.run([str(file), '--rga-config-file=' + str(config), '--rga-cache-path=' + str(scratch / 'cache'), str(fixture)], capture_output=True, timeout=15)
    return result.returncode == 0 and b'workspace-mcp-probe' in result.stdout


def native_tools(lock, downloads, target, offline):
    result = {}
    directory = downloads / "bin" / target / "sirchmunk"
    directory.mkdir(parents=True, exist_ok=True)
    for name in ("rg", "rga", "rga-preproc"):
        suffix = ".exe" if os.name == "nt" else ""
        found = shutil.which(name)
        cached = directory / (name + suffix)
        if not found and cached.exists():
            found = str(cached)
        if found:
            if check_native(found, name, downloads / 'cache/native-probe'):
                result[name] = str(Path(found).resolve())
                continue
        group = "rg" if name == "rg" else "rga"
        spec = lock["native"][group].get(target)
        if not spec:
            raise RuntimeError(f"No pinned {group} artifact for {target}; install a compatible local {name} first")
        archive = acquire(spec, downloads / "archives", offline)
        names = {("rg" if group == "rg" else "rga") + suffix}
        if group == "rga":
            names.add("rga-preproc" + suffix)
        if archive.suffix == ".zip":
            with zipfile.ZipFile(archive) as source:
                for member in source.infolist():
                    base = Path(member.filename).name
                    if base in names and not member.is_dir():
                        (directory / base).write_bytes(source.read(member))
        else:
            with tarfile.open(archive) as source:
                for member in source.getmembers():
                    base = Path(member.name).name
                    if base in names and member.isfile():
                        with source.extractfile(member) as data:
                            (directory / base).write_bytes(data.read())
        for executable in names:
            (directory / executable).chmod(0o755)
        if not check_native(cached, name, downloads / 'cache/native-probe'):
            raise RuntimeError(f'Native tool validation failed: {name}')
        result[name] = str(cached)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--downloads", required=True)
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--index-url", default="https://pypi.org/simple")
    args = parser.parse_args()
    root, downloads = Path(args.root).resolve(), Path(args.downloads).resolve()
    if not root.is_relative_to(downloads):
        raise ValueError("Python environment must be inside downloads")
    root.mkdir(parents=True, exist_ok=True)
    temporary = downloads / 'cache/tmp'
    temporary.mkdir(parents=True, exist_ok=True)
    os.environ.update({name: str(temporary) for name in ('TEMP', 'TMP', 'TMPDIR')})
    lock = json.loads((Path(__file__).parent.parent / "upstream-lock.json").read_text(encoding="utf-8"))
    with exclusive_file(root / ".prepare.lock"), (root / "prepare.log").open("a", encoding="utf-8") as log:
        def run(command):
            subprocess.run(command, check=True, stdout=log, stderr=log)
        venv = root / "venv"
        python = venv / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
        if not python.exists():
            run([sys.executable, "-I", "-m", "venv", str(venv)])
        ready = root / "prepared.json"
        old = json.loads(ready.read_text(encoding="utf-8")) if ready.exists() else {}
        if old.get("wheel_sha256") != lock["wheel"]["sha256"]:
            wheel = acquire(lock["wheel"], downloads / "wheels", args.offline)
            command = [str(python), "-I", "-m", "pip", "--isolated", "install", "--disable-pip-version-check",
                       "--cache-dir", str(downloads / "cache/pip"), "--index-url", args.index_url]
            if args.offline:
                command += ["--no-index", "--find-links", str(downloads / "wheels")]
            run(command + [str(wheel) + "[mcp]"])
        run([str(python), "-I", "-m", "pip", "check"])
        # Importability and the actual SDK signature matter more than pip exit status.
        probe = "import inspect, importlib.metadata; from sirchmunk import AgenticSearch; import sentence_transformers, modelscope; assert importlib.metadata.version('sirchmunk') == '" + lock["version"] + "'; assert 'enable_knowledge_evolution' in inspect.signature(AgenticSearch).parameters"
        run([str(python), "-I", "-c", probe])
        target = ("win32" if os.name == "nt" else "darwin" if sys.platform == "darwin" else "linux") + "-" + ("arm64" if platform.machine().lower() in ("aarch64", "arm64") else "x64")
        binaries = native_tools(lock, downloads, target, args.offline)
        frozen = subprocess.check_output([str(python), "-I", "-m", "pip", "freeze"], text=True)
        (root / "installed-requirements.txt").write_text(frozen, encoding="utf-8")
        atomic_json(ready, {"python": str(python), "version": lock["version"], "wheel_sha256": lock["wheel"]["sha256"], "binaries": binaries})


if __name__ == "__main__":
    contain_process_tree()
    main()
