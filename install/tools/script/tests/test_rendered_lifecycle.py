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
import time
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--publish-dir", required=True)
parser.add_argument("--backends", default="none,systemd,supervisord")
options, unittest_args = parser.parse_known_args()
ROOT = Path(options.publish_dir).resolve()
SOURCE = Path(__file__).resolve().parents[1]
ENV = dict(os.environ, ATF_SUPERVISOR_DIR=str(ROOT / "supervisor"))
BACKENDS = options.backends.split(",")


def run(*args, backend="none", check=True, cwd=None):
    result = subprocess.run([str(x) for x in args], env=dict(ENV, ATF_SERVICE_MANAGER=backend),
                            cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=60)
    if check and result.returncode:
        raise AssertionError(f"{args}: exit {result.returncode}\n{result.stdout}")
    return result


def eventually(predicate):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError("observable condition was not reached")


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
events=pid.with_suffix('.events')
request=pid.with_suffix('.stop-request')
if 'stop' in args:
    with events.open('a') as output:
        output.write(json.dumps(['stop-command',args[args.index('stop')+1:]])+'\\n')
    if pid.with_suffix('.reject-stop').exists():
        sys.exit(23)
    request.touch()
    sys.exit(0)
if '--fail-start' in args:
    error.write_text('deliberate startup failure')
    sys.exit(71)
pid.write_text(str(os.getpid()))
pid.with_suffix('.arguments').write_text(json.dumps(args))
events.write_text('')
request.unlink(missing_ok=True)
# Match common.sh's supported wrapper ownership check via an open executable file.
executable_handle=open(__file__)
def terminated(*_):
    with events.open('a') as output:
        output.write(json.dumps(['signal','TERM'])+'\\n')
    sys.exit(0)
signal.signal(signal.SIGTERM, terminated)
while not request.exists():
    time.sleep(0.02)
""")
        executable.chmod(0o755)

    def tearDown(self):
        run("bash", ROOT / "kill_all.sh", backend="invalid")

    def test_all_scripts_pin_backend_and_stop_every_instance(self):
        for backend in BACKENDS:
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
        for backend in BACKENDS:
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

    def test_stop_runs_business_command_with_arguments(self):
        script = self.starts[0]
        instance = script.stem.removeprefix("start_")
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                run("bash", script, backend=backend)
                extra = ["--upgrade", "space ' % value", ""]
                run("bash", script.parent / f"stop_{instance}.sh", *extra, backend="invalid")
                events = [json.loads(line) for line in (script.parent / f"echosvr_{instance}.events").read_text().splitlines()]
                self.assertEqual(events, [["stop-command", extra]])

    def test_failed_business_stop_preserves_process_and_can_be_retried(self):
        script = self.starts[0]
        instance = script.stem.removeprefix("start_")
        state = script.parent / f"echosvr_{instance}.pid.service"
        rejected = script.parent / f"echosvr_{instance}.reject-stop"
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                run("bash", script, backend=backend)
                pid = run("bash", ROOT / "tools/script/service-manager.sh", "pid", state).stdout.strip()
                rejected.touch()
                try:
                    failed = run("bash", script.parent / f"stop_{instance}.sh", backend="invalid", check=False)
                    self.assertNotEqual(failed.returncode, 0)
                    self.assertEqual(run("bash", ROOT / "tools/script/service-manager.sh", "pid", state).stdout.strip(), pid)
                    self.assertFalse((state / "stopped").exists())
                    events = [json.loads(line) for line in (script.parent / f"echosvr_{instance}.events").read_text().splitlines()]
                    self.assertEqual(events, [["stop-command", []]])
                    refused_start = run("bash", script, backend=backend, check=False)
                    self.assertNotEqual(refused_start.returncode, 0)
                    self.assertEqual(run("bash", ROOT / "tools/script/service-manager.sh", "pid", state).stdout.strip(), pid)
                finally:
                    rejected.unlink()
                run("bash", script.parent / f"stop_{instance}.sh", backend="invalid")
                events = [json.loads(line) for line in (script.parent / f"echosvr_{instance}.events").read_text().splitlines()]
                self.assertEqual(events, [["stop-command", []], ["stop-command", []]])

    def test_start_recovers_after_failed_stop_and_process_exit(self):
        script = self.starts[0]
        instance = script.stem.removeprefix("start_")
        state = script.parent / f"echosvr_{instance}.pid.service"
        rejected = script.parent / f"echosvr_{instance}.reject-stop"
        manager = ROOT / "tools/script/service-manager.sh"
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                run("bash", script, backend=backend)
                old_pid = run("bash", manager, "pid", state).stdout.strip()
                rejected.touch()
                try:
                    self.assertNotEqual(run("bash", script.parent / f"stop_{instance}.sh",
                                            backend="invalid", check=False).returncode, 0)
                finally:
                    rejected.unlink()
                # The business process exits after a failed stop; no signal is used.
                script.with_name(f"echosvr_{instance}.stop-request").touch()
                eventually(lambda: run("bash", manager, "pid", state, check=False).returncode != 0)
                run("bash", script, "--custom", "recovered", backend=backend)
                self.assertNotEqual(run("bash", manager, "pid", state).stdout.strip(), old_pid)
                observed = json.loads(script.with_name(f"echosvr_{instance}.arguments").read_text())
                self.assertEqual(observed[-2:], ["--custom", "recovered"])
                self.assertTrue((state / "enabled").exists())
                self.assertFalse((state / "stop-command.error").exists())
                run("bash", script.parent / f"stop_{instance}.sh", backend="invalid")
                events = [json.loads(line) for line in script.with_name(f"echosvr_{instance}.events").read_text().splitlines()]
                self.assertEqual(events, [["stop-command", []]])

    def test_single_service_start_stop_repairs_damaged_control_data(self):
        script = self.starts[0]
        other = self.starts[1]
        instance = script.stem.removeprefix("start_")
        state = script.parent / f"echosvr_{instance}.pid.service"
        manager = ROOT / "tools/script/service-manager.sh"
        app_pid = script.with_name(f"echosvr_{instance}.pid")
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                run("bash", other, backend=backend)
                other_instance = other.stem.removeprefix("start_")
                other_pid = other.with_name(f"echosvr_{other_instance}.pid").read_text()
                for damage in ("metadata", "markers", "pid-and-stop-receipt", "record-checksum"):
                    with self.subTest(damage=damage):
                        run("bash", script, backend=backend)
                        pid = app_pid.read_text()
                        expected_key = (state / "key").read_text()
                        if damage == "metadata":
                            (state / "backend").unlink()
                            for field in ("key", "unit", "controller", "kind", "stop-script"):
                                (state / field).write_text("damaged\n")
                        elif damage == "markers":
                            (state / "stopped").write_text("yes\n")
                            (state / "enabled").unlink()
                        elif damage == "pid-and-stop-receipt":
                            app_pid.write_text("invalid-pid\n")
                            (state / "pid").write_text(str(os.getpid()) + "\n")
                            (state / "identity").write_text("wrong process\n")
                            (state / "stop-command.done").touch()
                        else:
                            (state / "lifecycle.record").write_bytes(b"truncated record")
                            (state / "backend").write_text("damaged\n")
                        if damage in ("markers", "record-checksum"):
                            run("bash", script, backend=backend)
                            self.assertEqual(app_pid.read_text(), pid)
                            self.assertEqual((state / "key").read_text(), expected_key)
                        extra = ["--upgrade", damage, ""]
                        run("bash", script.parent / f"stop_{instance}.sh", *extra, backend="invalid")
                        run("bash", script.parent / f"stop_{instance}.sh", backend="invalid")
                        self.assertNotEqual(run("bash", manager, "pid", state, check=False).returncode, 0)
                        events = [json.loads(line) for line in script.with_name(f"echosvr_{instance}.events").read_text().splitlines()]
                        self.assertEqual(events, [["stop-command", extra]])
                        self.assertEqual(other.with_name(f"echosvr_{other_instance}.pid").read_text(), other_pid)
                        run("bash", script, backend=backend)
                        run("bash", script.parent / f"stop_{instance}.sh", backend="invalid")
                run("bash", other.parent / f"stop_{other_instance}.sh", backend="invalid")

    def test_unrecoverable_records_fail_without_stopping_a_live_service(self):
        script = self.starts[0]
        instance = script.stem.removeprefix("start_")
        state = script.parent / f"echosvr_{instance}.pid.service"
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                run("bash", script, backend=backend)
                pid = script.with_name(f"echosvr_{instance}.pid").read_text()
                saved = {}
                try:
                    for name in ("lifecycle.record", "lifecycle.record.bak"):
                        path = state / name
                        saved[name] = path.read_bytes()
                        path.write_bytes(b"damaged record")
                    failed = run("bash", script.parent / f"stop_{instance}.sh", backend="invalid", check=False)
                    self.assertNotEqual(failed.returncode, 0)
                    self.assertEqual(script.with_name(f"echosvr_{instance}.pid").read_text(), pid)
                    self.assertEqual(script.with_name(f"echosvr_{instance}.events").read_text(), "")
                finally:
                    for name, content in saved.items():
                        (state / name).write_bytes(content)
                run("bash", script.parent / f"stop_{instance}.sh", backend="invalid")

    def test_unverifiable_process_does_not_fall_back_to_a_stop_signal(self):
        script = self.starts[0]
        instance = script.stem.removeprefix("start_")
        state = script.parent / f"echosvr_{instance}.pid.service"
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                run("bash", script, backend=backend)
                files = [state / name for name in ("process.record", "pid", "identity")]
                files.append(script.with_name(f"echosvr_{instance}.pid"))
                saved = {path: path.read_bytes() for path in files}
                try:
                    for path in files:
                        path.write_text("damaged\n")
                    failed = run("bash", script.parent / f"stop_{instance}.sh", backend="invalid", check=False)
                    self.assertNotEqual(failed.returncode, 0)
                    self.assertEqual(script.with_name(f"echosvr_{instance}.events").read_text(), "")
                finally:
                    for path, content in saved.items():
                        path.write_bytes(content)
                run("bash", script.parent / f"stop_{instance}.sh", backend="invalid")

    def test_deployment_link_keeps_names_across_package_versions(self):
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                work = ROOT / f"versioned-{backend}"
                work.mkdir(exist_ok=True)
                deployment = work / "deployment"
                keys = []
                for version in ("v1", "v2"):
                    release = work / version
                    shutil.copytree(ROOT / "echosvr", release / "echosvr", dirs_exist_ok=True,
                                    ignore=shutil.ignore_patterns("*.pid*", "*.events", "*.arguments", "*.stop-request"))
                    shutil.copytree(SOURCE, release / "tools/script", dirs_exist_ok=True)
                    (release / "lib").mkdir(exist_ok=True)
                    deployment.unlink(missing_ok=True)
                    deployment.symlink_to(release, target_is_directory=True)
                    script = deployment / "echosvr/bin" / self.starts[0].name
                    instance = script.stem.removeprefix("start_")
                    state = script.parent / f"echosvr_{instance}.pid.service"
                    try:
                        run("bash", os.path.relpath(script, ROOT), "--custom", version, backend=backend, cwd=ROOT)
                        keys.append((state / "key").read_text().strip())
                        # Emulate a shell entered through the deployment link, as in manual operations.
                        run("bash", "-c", 'cd "$1" && exec bash "$2" --upgrade', "stop-test",
                            script.parent, f"./stop_{instance}.sh", backend="invalid")
                        self.assertTrue((state / "stopped").exists())
                    finally:
                        run("bash", deployment / "tools/script/service-manager.sh", "kill", state, backend="invalid")
                self.assertEqual(keys[0], keys[1])

    def test_stop_adopts_business_command_for_pre_callback_lifecycle(self):
        script = self.starts[0]
        instance = script.stem.removeprefix("start_")
        state = script.parent / f"echosvr_{instance}.pid.service"
        manager = ROOT / "tools/script/service-manager.sh"
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                # The previous public start API recorded only the foreground command.
                run("bash", manager, "start", state, "legacy-stop", script.parent, "--",
                    "/bin/bash", script, "--atapp-foreground", backend=backend)
                eventually(lambda: (script.parent / f"echosvr_{instance}.pid").exists() and
                           (script.parent / f"echosvr_{instance}.pid").read_text().strip() ==
                           run("bash", manager, "pid", state).stdout.strip())
                run("bash", script.parent / f"stop_{instance}.sh", "--upgrade", backend="invalid")
                events = [json.loads(line) for line in (script.parent / f"echosvr_{instance}.events").read_text().splitlines()]
                self.assertEqual(events, [["stop-command", ["--upgrade"]]])

    def test_direct_manager_stop_after_crash_uses_business_command(self):
        script = self.starts[0]
        instance = script.stem.removeprefix("start_")
        state = script.parent / f"echosvr_{instance}.pid.service"
        manager = ROOT / "tools/script/service-manager.sh"
        for backend in BACKENDS:
            if backend == "none":
                continue
            with self.subTest(backend=backend):
                run("bash", script, backend=backend)
                old_pid = int(run("bash", manager, "pid", state).stdout)
                os.kill(old_pid, 9)
                def restarted():
                    result = run("bash", manager, "pid", state, check=False)
                    app_pid = (script.parent / f"echosvr_{instance}.pid").read_text().strip()
                    return result.returncode == 0 and result.stdout.strip() == app_pid and int(app_pid) != old_pid
                eventually(restarted)
                if backend == "systemd":
                    run("systemctl", "--user", "stop", (state / "unit").read_text().strip())
                else:
                    run("supervisorctl", "-c", ROOT / "supervisor/supervisord.conf", "stop", (state / "key").read_text().strip())
                events = [json.loads(line) for line in (script.parent / f"echosvr_{instance}.events").read_text().splitlines()]
                self.assertEqual(events, [["stop-command", []]])
                self.assertNotEqual(run("bash", manager, "pid", state, check=False).returncode, 0)
                run("bash", manager, "stop", state, backend="invalid")


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
