#!/usr/bin/env python3
"""Acceptance against existing etcd binaries and an already cached Redis image."""
import argparse
import os
from pathlib import Path
import shutil
import signal
import socket
import subprocess
import sys
import time
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--work-dir", required=True)
parser.add_argument("--etcd-bin-dir", required=True)
parser.add_argument("--container-engine", required=True)
options, unittest_args = parser.parse_known_args()
ROOT = Path(options.work_dir).resolve()
ROOT.mkdir(parents=True, exist_ok=True)
SCRIPTS = Path(__file__).resolve().parents[1]
# These fixtures use cached images and loopback ports; WSL need not have network-online.target.
ENV = dict(os.environ, ATF_SUPERVISOR_DIR=str(ROOT / "supervisor"), ATF_QUADLET_WAIT_NETWORK="0")


def run(*args, env=None, check=True):
    result = subprocess.run([str(a) for a in args], env=env or ENV, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    if check and result.returncode:
        raise AssertionError(f"{args}: exit {result.returncode}\n{result.stdout}")
    return result


def eventually(predicate):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(0.1)
    raise AssertionError("dependency did not reach expected state")


def port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Dependencies(unittest.TestCase):
    def test_existing_image_wrapper_restarts_when_background_service_crashes(self):
        engine = options.container_engine
        work = ROOT / "legacy-image"
        work.mkdir(exist_ok=True)
        legacy = work / "legacy-entrypoint.sh"
        legacy.write_text('''#!/bin/bash
case "$1" in
  start)
    (while [[ ! -f /fixture/crash-request ]]; do sleep 0.05; done
     rm /fixture/crash-request; exit 77) &
    echo $! > /service.pid
    exec sleep 600 ;;
  health_check)
    [[ -f /service.pid ]] || exit 1
    pid=$(cat /service.pid)
    [[ -r /proc/$pid/stat ]] || exit 1
    [[ $(cut -d ' ' -f 3 /proc/$pid/stat) != Z ]] ;;
  stop) kill "$(cat /service.pid)" ;;
esac
''')
        state = work / "service"
        crash_request = work / "crash-request"
        crash_request.unlink(missing_ok=True)
        try:
            run("bash", SCRIPTS / "service-manager.sh", "start", state, "legacy-image", work,
                "--container", engine, "docker.io/library/redis:7.2.16", "--",
                "--volume", f"{legacy}:/entrypoint.sh:ro",
                "--volume", f"{work}:/fixture",
                "--volume", f"{SCRIPTS / 'container-entrypoint.sh'}:/atf-entrypoint.sh:ro",
                "--entrypoint", "/bin/bash", "--", "/atf-entrypoint.sh", "start",
                env=dict(ENV, ATF_SERVICE_MANAGER="supervisord"))
            name = (state / "key").read_text().strip()
            # An attached container client can be RUNNING before application readiness.
            eventually(lambda: '[container-entrypoint] service is healthy' in
                       run(engine, "logs", name, check=False).stdout)
            first = run(engine, "inspect", "-f", "{{.Id}}", name).stdout.strip()
            # Do not require an exec acknowledgement from a container being removed.
            # The mounted request deterministically makes only the background service exit.
            crash_request.touch()
            def restarted():
                result = run(engine, "inspect", "-f", "{{.Id}}", name, check=False)
                return (result.returncode == 0 and result.stdout.strip() != first and
                        run(engine, "exec", name, "bash", "/entrypoint.sh", "health_check", check=False).returncode == 0)
            eventually(restarted)
            self.assertFalse(crash_request.exists())
            run("bash", SCRIPTS / "service-manager.sh", "stop", state)
            self.assertNotEqual(run(engine, "inspect", name, check=False).returncode, 0)
        finally:
            run("bash", SCRIPTS / "service-manager.sh", "kill", state)

    def test_container_command_environment_and_volume_arguments(self):
        engine = options.container_engine
        for backend in ("systemd", "supervisord"):
            with self.subTest(backend=backend):
                work = ROOT / f"container-{backend} space %"
                work.mkdir(exist_ok=True)
                state = work / "service"
                payload = "space ' \" % $HOME ; literal\nnext line"
                try:
                    run("bash", SCRIPTS / "service-manager.sh", "start", state, "argument-test", work,
                        "--container", engine, "docker.io/library/redis:7.2.16", "--",
                        "--volume", f"{work}:/output", "--env", "ARGUMENT_VALUE=value % $HOME",
                        "--", "sh", "-c",
                        'printf "%s" "$1" > /output/argument; printf "%s" "$ARGUMENT_VALUE" > /output/environment; exec sleep 600',
                        "fixture", payload, env=dict(ENV, ATF_SERVICE_MANAGER=backend))
                    eventually(lambda: (work / "argument").exists())
                    self.assertEqual((work / "argument").read_text(), payload)
                    self.assertEqual((work / "environment").read_text(), "value % $HOME")
                    run("bash", SCRIPTS / "service-manager.sh", "stop", state,
                        env=dict(ENV, ATF_SERVICE_MANAGER="invalid", CONTAINER_HOST="unix:///does-not-exist"))
                    name = (state / "key").read_text().strip()
                    self.assertNotEqual(run(engine, "inspect", name, check=False).returncode, 0)
                finally:
                    run("bash", SCRIPTS / "service-manager.sh", "kill", state)

    def test_etcd_health_restart_and_recorded_stop(self):
        for backend in ("none", "systemd", "supervisord"):
            with self.subTest(backend=backend):
                work = ROOT / f"etcd-{backend}"
                work.mkdir(exist_ok=True)
                for name in ("etcd", "etcdctl"):
                    shutil.copy2(Path(options.etcd_bin_dir) / name, work / name)
                script = SCRIPTS / "etcd/setup-etcd.sh"
                state = work / "service"
                client, peer = port(), port()
                try:
                    run("sh", script, "start", "--work-dir", work, "--service-manager", backend,
                        "--client-port", client, "--peer-port", peer)
                    health = lambda: run(work / "etcdctl", f"--endpoints=http://127.0.0.1:{client}",
                                         "endpoint", "health", check=False).returncode == 0
                    self.assertTrue(health())
                    pid = int(run("bash", SCRIPTS / "service-manager.sh", "pid", state).stdout)
                    if backend != "none":
                        os.kill(pid, signal.SIGKILL)
                        def restarted():
                            result = run("bash", SCRIPTS / "service-manager.sh", "pid", state, check=False)
                            return result.returncode == 0 and int(result.stdout) != pid and health()
                        eventually(restarted)
                    run("sh", script, "stop", "--work-dir", work, "--service-manager", "invalid")
                    self.assertFalse(health())
                    self.assertNotEqual(run("bash", SCRIPTS / "service-manager.sh", "pid", state,
                                            check=False).returncode, 0)
                finally:
                    run("bash", SCRIPTS / "service-manager.sh", "kill", state)

    def test_cached_redis_container_crash_and_force_stop(self):
        engine = options.container_engine
        run(engine, "image", "inspect", "docker.io/library/redis:7.2.16")
        for backend in ("none", "systemd", "supervisord"):
            with self.subTest(backend=backend):
                work = ROOT / f"redis-{backend}"
                state = work / "service"
                script = SCRIPTS / "redis/redis.sh"
                try:
                    run("sh", script, "start", "--work-dir", work, "--service-manager", backend,
                        "--client-port", port(), "--container-engine", engine)
                    name = (state / "key").read_text().strip()
                    if backend == "systemd" and Path(engine).name == "podman":
                        self.assertEqual((state / "kind").read_text().strip(), "quadlet")
                        quadlet = Path((state / "quadlet-file").read_text().strip())
                        self.assertTrue(quadlet.is_file())
                        self.assertTrue(quadlet.is_symlink())
                        self.assertEqual(quadlet.resolve(), (state / "unit.container").resolve())
                        dropins = Path(str(quadlet) + ".d")
                        self.assertTrue(dropins.is_symlink())
                        self.assertEqual(dropins.resolve(), (state / "unit.container.d").resolve())
                        self.assertTrue((dropins / "10-atsf4g-enable.conf").is_file())
                        unit = (state / "unit").read_text().strip()
                        self.assertEqual(run("systemctl", "--user", "is-enabled", unit).stdout.strip(), "generated")
                    first_id = run(engine, "inspect", "-f", "{{.Id}}", name).stdout.strip()
                    self.assertEqual(run(engine, "exec", name, "redis-cli", "PING").stdout.strip(), "PONG")
                    if backend != "none":
                        run(engine, "kill", name)
                        def restarted():
                            result = run(engine, "inspect", "-f", "{{.Id}}", name, check=False)
                            return (result.returncode == 0 and result.stdout.strip() != first_id and
                                    run(engine, "exec", name, "redis-cli", "PING", check=False).stdout.strip() == "PONG")
                        eventually(restarted)
                    run("sh", script, "kill", "--work-dir", work, "--service-manager", "invalid",
                        "--container-engine", "does-not-exist")
                    self.assertNotEqual(run(engine, "container", "inspect", name, check=False).returncode, 0)
                    self.assertFalse((state / "enabled").exists())
                    if backend == "systemd" and Path(engine).name == "podman":
                        self.assertFalse(quadlet.exists())
                        self.assertFalse(os.path.lexists(dropins))
                        self.assertNotEqual(run("systemctl", "--user", "is-active", unit, check=False).returncode, 0)
                        self.assertEqual(run("systemctl", "--user", "show", "-p", "ActiveState", "--value", unit).stdout.strip(),
                                         "inactive")
                    run("sh", script, "stop", "--work-dir", work)
                finally:
                    run("bash", SCRIPTS / "service-manager.sh", "kill", state)


if __name__ == "__main__":
    try:
        program = unittest.main(argv=[sys.argv[0], *unittest_args], verbosity=2, exit=False)
    finally:
        conf = ROOT / "supervisor/supervisord.conf"
        if conf.exists():
            run("supervisorctl", "-c", conf, "shutdown", check=False)
        for key_file in ROOT.glob("*/service/key"):
            link = Path.home() / ".local/state/atsf4g/instances" / key_file.read_text().strip()
            if link.is_symlink():
                link.unlink()
    sys.exit(0 if program.result.wasSuccessful() else 1)
