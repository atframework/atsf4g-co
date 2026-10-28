#!/usr/bin/env python3
"""Execute a rendered two-instance echosvr deployment with a foreground CLI fixture.

The fixture implements the PID/startup-error interface consumed by generated shell
scripts; this checks script integration, not the echosvr business implementation.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--publish-dir", required=True)
options, unittest_args = parser.parse_known_args()
ROOT = Path(options.publish_dir).resolve()
SOURCE = Path(__file__).resolve().parents[1]
ENV = dict(os.environ, ATF_SUPERVISOR_DIR=str(ROOT / "supervisor"))


def run(*args, backend="none", check=True):
    result = subprocess.run([str(x) for x in args], env=dict(ENV, ATF_SERVICE_MANAGER=backend),
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=60)
    if check and result.returncode:
        raise AssertionError(f"{args}: exit {result.returncode}\n{result.stdout}")
    return result


class RenderedScripts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        shutil.copytree(SOURCE, ROOT / "tools/script", dirs_exist_ok=True)
        (ROOT / "lib").mkdir(exist_ok=True)
        cls.starts = sorted((ROOT / "echosvr/bin").glob("start_*.sh"))
        if len(cls.starts) != 2:
            raise AssertionError("render exactly two echosvr instances for this test")
        executable = ROOT / "echosvr/bin/echosvrd"
        executable.write_text("""#!/usr/bin/env python3
import json,os,signal,sys,time
from pathlib import Path
args=sys.argv[1:]
pid=Path(args[args.index('-p')+1])
error=Path(args[args.index('--startup-error-file')+1])
if '--fail-start' in args:
    error.write_text('deliberate startup failure')
    sys.exit(71)
pid.write_text(str(os.getpid()))
pid.with_suffix('.arguments').write_text(json.dumps(args))
signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
time.sleep(600)
""")
        executable.chmod(0o755)

    def tearDown(self):
        run("bash", ROOT / "kill_all.sh", backend="invalid")

    def test_all_scripts_pin_backend_and_stop_every_instance(self):
        for backend in ("none", "systemd", "supervisord"):
            with self.subTest(backend=backend):
                run("bash", ROOT / "start_all.sh", "-m", backend)
                records = list((ROOT / "echosvr/bin").glob("*.pid.service"))
                self.assertEqual(len(records), 2)
                for state in records:
                    self.assertEqual((state / "backend").read_text().strip(), backend)
                    run("bash", ROOT / "tools/script/service-manager.sh", "pid", state)
                run("bash", ROOT / "stop_all.sh", backend="invalid")
                for state in records:
                    self.assertFalse((state / "enabled").exists())
                run("bash", ROOT / "start_all.sh", "-m", backend)
                run("bash", ROOT / "kill_all.sh", backend="invalid")
                for state in records:
                    self.assertNotEqual(run("bash", ROOT / "tools/script/service-manager.sh", "pid", state,
                                            check=False).returncode, 0)

    def test_instance_arguments_and_failed_readiness_cleanup(self):
        script = self.starts[0]
        instance = script.stem.removeprefix("start_")
        state = script.parent / f"echosvr_{instance}.pid.service"
        for backend in ("none", "systemd", "supervisord"):
            with self.subTest(backend=backend):
                extra = ["--custom", "space ' \" % value", ""]
                run("bash", script, *extra, backend=backend)
                observed = json.loads((script.parent / f"echosvr_{instance}.arguments").read_text())
                self.assertEqual(observed[-3:], extra)
                run("bash", script.parent / f"stop_{instance}.sh", backend="invalid")
                failed = run("bash", script, "--fail-start", backend=backend, check=False)
                self.assertNotEqual(failed.returncode, 0)
                self.assertFalse((state / "enabled").exists())
                self.assertNotEqual(run("bash", ROOT / "tools/script/service-manager.sh", "pid", state,
                                        check=False).returncode, 0)


if __name__ == "__main__":
    try:
        program = unittest.main(argv=[sys.argv[0], *unittest_args], verbosity=2, exit=False)
    finally:
        conf = ROOT / "supervisor/supervisord.conf"
        if conf.exists():
            run("supervisorctl", "-c", conf, "shutdown", check=False)
        for key_file in ROOT.glob("echosvr/bin/*.pid.service/key"):
            link = Path.home() / ".local/state/atsf4g/instances" / key_file.read_text().strip()
            if link.is_symlink():
                link.unlink()
    sys.exit(0 if program.result.wasSuccessful() else 1)
