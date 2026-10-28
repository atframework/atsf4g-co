#!/usr/bin/env python3
"""Real lifecycle acceptance tests; requires a user systemd and Supervisor in PATH.

Run with --work-dir <BUILD_DIR>/_agent_tmp/service-manager-tests. No system daemon
or existing deployment is used. The private Supervisor is shut down on completion.
"""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time
import unittest

MANAGER = Path(__file__).resolve().parents[1] / "service-manager.sh"
PARSER = argparse.ArgumentParser()
PARSER.add_argument("--work-dir", required=True)
PARSER.add_argument("--backends", default="none,systemd,supervisord")
OPTIONS, UNITTEST_ARGS = PARSER.parse_known_args()
ROOT = Path(OPTIONS.work_dir).resolve()
ROOT.mkdir(parents=True, exist_ok=True)
BACKENDS = OPTIONS.backends.split(",")
ENV = dict(os.environ, ATF_SUPERVISOR_DIR=str(ROOT / "supervisor"))


def run(*args, env=None, check=True):
    result = subprocess.run([str(x) for x in args], env=env or ENV,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=45)
    if check and result.returncode:
        raise AssertionError(f"{args}: exit {result.returncode}\n{result.stdout}")
    return result


def eventually(predicate, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(0.05)
    raise AssertionError("observable condition was not reached")


class Lifecycle(unittest.TestCase):
    def setUp(self):
        self.states = []

    def tearDown(self):
        for state in self.states:
            run("bash", MANAGER, "kill", state)

    def state(self, backend, suffix=""):
        state = ROOT / f"{self._testMethodName}-{backend}{suffix}"
        self.states.append(state)
        return state

    def start(self, state, backend, ignore_term=False):
        args = ["argument with spaces", "", "quote'\";$(touch should-not-exist)%", "line\nbreak"]
        code = ("import os,sys,json,signal,time; "
                "signal.signal(signal.SIGTERM, signal.SIG_IGN) if sys.argv[1]=='1' else None; "
                "open('observed.json','w').write(json.dumps([sys.argv[2:],os.environ.get('ATAPP_TEST_VALUE')])); "
                "time.sleep(600)")
        state.mkdir(parents=True, exist_ok=True)
        env = dict(ENV, ATF_SERVICE_MANAGER=backend, ATAPP_TEST_VALUE="space ' % value")
        run("bash", MANAGER, "start", state, "acceptance-service.1", state, "--",
            sys.executable, "-c", code, str(int(ignore_term)), *args, env=env)
        eventually(lambda: (state / "observed.json").exists())
        self.assertEqual(json.loads((state / "observed.json").read_text()),
                         [args, "space ' % value"])
        return self.pid(state)

    def pid(self, state):
        result = run("bash", MANAGER, "pid", state, check=False)
        return int(result.stdout.strip()) if result.returncode == 0 else 0

    def assert_stopped(self, state, backend):
        self.assertEqual(self.pid(state), 0)
        self.assertFalse((state / "enabled").exists())
        if backend == "systemd":
            unit = (state / "unit").read_text().strip()
            self.assertNotEqual(run("systemctl", "--user", "is-enabled", unit,
                                    check=False).returncode, 0)
            self.assertNotEqual(run("systemctl", "--user", "is-active", unit,
                                    check=False).returncode, 0)
        elif backend == "supervisord":
            name = (state / "key").read_text().strip()
            self.assertIn("no such process", run("supervisorctl", "-c",
                          ROOT / "supervisor/supervisord.conf", "status", name,
                          check=False).stdout)

    def test_auto_prefers_usable_user_systemd_and_explicit_none(self):
        self.assertEqual(run("bash", MANAGER, "select", "none").stdout.strip(), "none")
        self.assertNotEqual(run("bash", MANAGER, "select", "typo", check=False).returncode, 0)
        if "systemd" in BACKENDS:
            self.assertEqual(run("bash", MANAGER, "select", "auto").stdout.strip(), "systemd")

    def test_systemd_keeps_unit_in_package_and_only_registers_a_link(self):
        if "systemd" not in BACKENDS:
            return
        state = self.state("systemd", " package % ${HOME}")
        self.assertGreater(self.start(state, "systemd"), 0)
        unit = (state / "unit").read_text().strip()
        unit_file = state / unit
        self.assertTrue(unit_file.is_file())
        fragment = run("systemctl", "--user", "show", "-p", "FragmentPath", "--value", unit).stdout.strip()
        self.assertEqual(Path(fragment).resolve(), unit_file.resolve())
        link = Path((state / "unit-link").read_text().strip())
        self.assertTrue(link.is_symlink())
        self.assertEqual(link.resolve(), unit_file.resolve())
        self.assertFalse((state / "registry-link").exists())
        run("bash", MANAGER, "stop", state)
        self.assert_stopped(state, "systemd")
        self.assertFalse(os.path.lexists(link))
        self.assertTrue(unit_file.is_file())

    def test_default_supervisor_configuration_stays_in_its_package(self):
        if "supervisord" not in BACKENDS:
            return
        for layout in ("directory", "linked-tools"):
            with self.subTest(layout=layout):
                package = ROOT / f"default package % {layout}"
                scripts = package / "tools/script"
                if layout == "directory":
                    shutil.copytree(MANAGER.parent, scripts, dirs_exist_ok=True)
                else:
                    scripts.parent.mkdir(parents=True, exist_ok=True)
                    if not scripts.is_symlink():
                        scripts.symlink_to(MANAGER.parent, target_is_directory=True)
                state = package / "runtime/service"
                self.states.append(state)
                env = dict(ENV, ATF_SERVICE_MANAGER="supervisord")
                env.pop("ATF_SUPERVISOR_DIR", None)
                conf = package / "var/service-manager/supervisor/supervisord.conf"
                try:
                    run("bash", scripts / "service-manager.sh", "start", state, "package-default", package,
                        "--", sys.executable, "-c", "import time; time.sleep(600)", env=env)
                    self.assertGreater(self.pid(state), 0)
                    self.assertEqual(Path((state / "supervisor-dir").read_text().strip()), conf.parent)
                    self.assertTrue(conf.is_file())
                    run("bash", MANAGER, "stop", state)
                    self.assertEqual(self.pid(state), 0)
                    key = (state / "key").read_text().strip()
                    self.assertIn("no such process", run("supervisorctl", "-c", conf, "status", key,
                                                        check=False).stdout)
                finally:
                    try:
                        run("bash", MANAGER, "kill", state)
                    finally:
                        if conf.exists():
                            run("supervisorctl", "-c", conf, "shutdown", check=False)

    def test_concurrent_start_and_recorded_stop(self):
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                state = self.state(backend)
                pid = self.start(state, backend)
                with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
                    pids = list(pool.map(lambda _: self.start(state, backend), range(3)))
                self.assertEqual(pids, [pid] * 3)
                changed = dict(ENV, ATF_SERVICE_MANAGER="invalid",
                               ATF_SUPERVISOR_DIR=str(ROOT / "wrong-daemon"))
                self.assertNotEqual(run("bash", MANAGER, "start", state, "changed", state,
                                        "--", "/bin/false", env=changed, check=False).returncode, 0)
                self.assertEqual(self.pid(state), pid)
                run("bash", MANAGER, "stop", state, env=changed)
                run("bash", MANAGER, "stop", state, env=changed)
                self.assert_stopped(state, backend)

    def test_crash_restart_and_stop_during_restart(self):
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                state = self.state(backend)
                pid = self.start(state, backend)
                os.kill(pid, signal.SIGKILL)
                if backend == "none":
                    eventually(lambda: self.pid(state) == 0)
                else:
                    new_pid = eventually(lambda: (p if (p := self.pid(state)) and p != pid else 0))
                    os.kill(new_pid, signal.SIGKILL)
                run("bash", MANAGER, "stop", state,
                    env=dict(ENV, ATF_SERVICE_MANAGER="none"))
                self.assert_stopped(state, backend)

    def test_force_kill_ignores_term_and_does_not_restart(self):
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                state = self.state(backend)
                self.start(state, backend, ignore_term=True)
                begin = time.monotonic()
                run("bash", MANAGER, "kill", state, env=dict(ENV, ATF_SERVICE_MANAGER="typo"))
                self.assertLess(time.monotonic() - begin, 6)
                self.assert_stopped(state, backend)

    def test_stop_allows_explicit_backend_change(self):
        state = self.state("switch")
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                self.start(state, backend)
                self.assertEqual((state / "backend").read_text().strip(), backend)
                run("bash", MANAGER, "stop", state)
                self.assert_stopped(state, backend)

    def test_same_name_in_separate_paths_keeps_instances_isolated(self):
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                first = self.state(backend, " space % first")
                second = self.state(backend, " space % second")
                first_pid = self.start(first, backend)
                second_pid = self.start(second, backend)
                self.assertNotEqual(first_pid, second_pid)
                run("bash", MANAGER, "stop", first)
                self.assert_stopped(first, backend)
                self.assertEqual(self.pid(second), second_pid)

    def test_missing_command_does_not_enable_instance(self):
        for backend in BACKENDS:
            with self.subTest(backend=backend):
                state = self.state(backend)
                result = run("bash", MANAGER, "start", state, "acceptance-failure", ROOT,
                             "--", "/does/not/exist", env=dict(ENV, ATF_SERVICE_MANAGER=backend),
                             check=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(self.pid(state), 0)
                self.assertFalse((state / "enabled").exists())


if __name__ == "__main__":
    try:
        program = unittest.main(argv=[sys.argv[0], *UNITTEST_ARGS], verbosity=2, exit=False)
    finally:
        conf = ROOT / "supervisor/supervisord.conf"
        if conf.exists() and shutil.which("supervisorctl"):
            run("supervisorctl", "-c", conf, "shutdown", check=False)
    sys.exit(0 if program.result.wasSuccessful() else 1)
