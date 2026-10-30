#!/usr/bin/env python3
"""Archive and reset acceptance; isolated packages, real tar/zstd and user managers."""
import argparse
import concurrent.futures
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest

parser = argparse.ArgumentParser()
parser.add_argument("--work-dir", required=True)
options, args = parser.parse_known_args()
ROOT = Path(options.work_dir).resolve()
SOURCE = Path(__file__).resolve().parents[1]
REPO = SOURCE.parents[2]
ROOT.mkdir(parents=True, exist_ok=True)


def run(*args, env=None, check=True):
    result = subprocess.run([str(a) for a in args], env=env, capture_output=True, text=True, timeout=90)
    if check and result.returncode:
        raise AssertionError(f"{args}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result


class Maintenance(unittest.TestCase):
    def package(self, suffix=""):
        root = ROOT / (self._testMethodName + suffix)
        shutil.copytree(SOURCE, root / "tools/script", dirs_exist_ok=True)
        (root / "app/bin").mkdir(parents=True, exist_ok=True)
        (root / "app/cfg").mkdir(exist_ok=True)
        (root / "app/cfg/config.yaml").write_text("keep: true\n")
        return root

    def test_all_archives_exclude_runtime_without_mutating_the_source(self):
        root = self.package()
        for name in ("app/bin/app.pid.service/lifecycle.record", "app/bin/app.pid", "app/bin/app.pid.old",
                     "app/bin/app.pid.service/private.dbg", "custom-etcd/service/private.dbg",
                     "app/bin/app.startup-error", "otelcol/bin/otelcol.service/lifecycle.record",
                     "otelcol/bin/otelcol.pid", "var/service-manager/supervisor/programs/old.ini",
                     "var/service-manager/reset-backups/old/state", "custom-etcd/service/backend",
                     "custom-etcd/service/key", "custom-etcd/service/unit", "custom-etcd/service/kind",
                     "custom-etcd/etcd.pid", "custom-redis/service/lifecycle.record", "custom-redis/redis.cid",
                     "tools/script/prepare-package.old.lock", "app/log/runtime.log"):
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("copied runtime state\n")
        for name in ("lib/libsample.so", "lib/symbol with spaces.dbg", "otelcol/bin/otelcol-contrib",
                     "otelcol/cfg/config.yaml", "bin/flatc", "resource/service/keep.txt"):
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("keep\n")
        (root / "tools/script/update_dependency.sh").write_text("#!/bin/bash\nexit 0\n")
        pack = ROOT / "pack-test.sh"
        source = (REPO / "project/tools/pack_publish.sh.in").read_text()
        pack.write_text(source.replace("@PROJECT_NAME@", "acceptance").replace("@PROJECT_TOOL_OBJCOPY@", "")
                       .replace("@PROJECT_TOOL_READELF@", ""))
        output = ROOT / "archives"
        run("bash", pack, "-d", root, "-o", output)
        main = next(output.glob("*.package.tar.zst"))
        debug = next(output.glob("*.debug.tar.zst"))
        otel = next(output.glob("*.otel_bin.tar.zst"))
        members = {}
        for kind, archive in (("main", main), ("debug", debug), ("otel", otel)):
            members[kind] = run("tar", "--zstd", "-tf", archive).stdout.splitlines()
            self.assertFalse(any(".pid.service" in n or "lifecycle.record" in n or
                                 "otelcol.service" in n or "/var/service-manager" in n or
                                 n.endswith((".pid", ".pid.old", ".startup-error", ".cid", ".lock"))
                                 for n in members[kind]), members[kind])
        self.assertIn("./app/cfg/config.yaml", members["main"])
        self.assertIn("./tools/script/common/atsf4g@.service", members["main"])
        self.assertIn("./resource/service/keep.txt", members["main"])
        self.assertIn("./lib/symbol with spaces.dbg", members["debug"])
        self.assertIn("./otelcol/bin/otelcol-contrib", members["otel"])
        self.assertIn("./bin/flatc", members["otel"])
        self.assertTrue((root / "app/bin/app.pid.service/lifecycle.record").exists())
        self.assertFalse(any("/custom-etcd/service" in n or "/custom-redis/service" in n
                             for n in members["main"]))
        restored = ROOT / "unpacked-package"
        restored.mkdir(exist_ok=True)
        for archive in (main, otel): run("tar", "--zstd", "-xf", archive, "-C", restored)
        manager = restored / "tools/script/service-manager.sh"
        state = restored / "app/bin/app.pid.service"
        env = dict(os.environ, ATF_SERVICE_MANAGER="none")
        try:
            run("bash", manager, "start", state, "unpacked-acceptance", restored,
                "--", sys.executable, "-c", "import time; time.sleep(600)", env=env)
            self.assertEqual((state / "backend").read_text().strip(), "none")
        finally:
            run("bash", manager, "kill", state)

    def test_reset_relocated_corrupt_state_preserves_configuration_and_data(self):
        root = self.package("-[literal] space")
        state = root / "app/bin/app.pid.service"
        state.mkdir()
        (state / "lifecycle.record").write_bytes(b"foreign\0record")
        (state / "pid").write_text(str(os.getpid()))
        (state / "identity").write_text(run("ps", "-p", os.getpid(), "-o", "lstart=").stdout.strip() + "\n")
        (root / "app/bin/app.pid").write_text(str(os.getpid()))
        dependency = root / "redis/service"
        dependency.mkdir(parents=True)
        (dependency / "lifecycle.record").write_bytes(b"invalid")
        (root / "redis/data").mkdir()
        (root / "redis/data/dump.rdb").write_text("keep data")
        reset = root / "tools/script/reset-service-state.sh"
        run("bash", reset)
        self.assertTrue(state.exists())
        run("bash", reset, "--force")
        self.assertFalse(state.exists())
        self.assertFalse(dependency.exists())
        self.assertFalse((root / "app/bin/app.pid").exists())
        self.assertEqual((root / "app/cfg/config.yaml").read_text(), "keep: true\n")
        self.assertEqual((root / "redis/data/dump.rdb").read_text(), "keep data")
        backups = list((root / "var/service-manager/reset-backups").glob("*/app/bin/app.pid.service"))
        self.assertTrue(backups)
        run("bash", reset, "--force")
        self.assertEqual(list((root / "var/service-manager/reset-backups").glob("*/app/bin/app.pid.service")), backups)

    def test_reset_refuses_live_services_then_allows_stopped_private_supervisor(self):
        for backend in ("none", "systemd", "supervisord"):
            with self.subTest(backend=backend):
                root = self.package("-" + backend)
                manager = root / "tools/script/service-manager.sh"
                state = root / "app/bin/app.pid.service"
                env = dict(os.environ, ATF_SERVICE_MANAGER=backend,
                           ATF_SUPERVISOR_DIR=str(root / "var/service-manager/supervisor"))
                try:
                    run("bash", manager, "start", state, "maintenance-live", root, "--",
                        sys.executable, "-c", "import time; time.sleep(600)", env=env)
                    pid = run("bash", manager, "pid", state).stdout.strip()
                    refused = run("bash", root / "tools/script/reset-service-state.sh", "--force", check=False)
                    self.assertNotEqual(refused.returncode, 0)
                    self.assertEqual(run("bash", manager, "pid", state).stdout.strip(), pid)
                    (state / "pid").write_text("damaged\n")
                    refused = run("bash", root / "tools/script/reset-service-state.sh", "--force", check=False)
                    self.assertNotEqual(refused.returncode, 0)
                    run("bash", manager, "stop", state)
                    run("bash", root / "tools/script/reset-service-state.sh", "--force")
                    self.assertFalse(state.exists())
                finally:
                    if state.exists():
                        run("bash", manager, "kill", state)
                    conf = root / "var/service-manager/supervisor/supervisord.conf"
                    if conf.exists():
                        run("supervisorctl", "-c", conf, "shutdown", check=False)

    def test_reset_removes_only_owned_inactive_registration_links(self):
        root = self.package()
        other = self.package("-other")
        user_dir = Path(os.environ.get("XDG_CONFIG_HOME", str(Path.home() / ".config"))) / "systemd/user"
        user_dir.mkdir(parents=True, exist_ok=True)
        name = "atsf4g@maintenance-reset-owned-" + str(os.getpid()) + ".service"
        other_name = "atsf4g@maintenance-reset-other-" + str(os.getpid()) + ".service"
        links = []
        try:
            for package, unit in ((root, name), (other, other_name)):
                path = package / "app/bin/app.pid.service" / unit
                path.parent.mkdir(exist_ok=True)
                path.write_text("[Service]\nType=oneshot\nExecStart=/bin/true\n")
                link = user_dir / unit
                link.symlink_to(path)
                links.append(link)
            run("systemctl", "--user", "daemon-reload")
            run("bash", root / "tools/script/reset-service-state.sh", "--force")
            self.assertFalse(os.path.lexists(links[0]))
            self.assertTrue(links[1].is_symlink())
            self.assertTrue((other / "app/bin/app.pid.service").exists())
        finally:
            for link in links: link.unlink(missing_ok=True)
            run("systemctl", "--user", "daemon-reload")

    def test_reset_refuses_runtime_parent_links_outside_deployment(self):
        root = self.package()
        outside = ROOT / "outside-state"
        outside.mkdir(exist_ok=True)
        (outside / "foreign.pid.service").mkdir(exist_ok=True)
        (root / "app/bin").rmdir()
        (root / "app/bin").symlink_to(outside, target_is_directory=True)
        result = run("bash", root / "tools/script/reset-service-state.sh", "--force", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue((outside / "foreign.pid.service").exists())
        root = self.package("-backup")
        runtime = root / "app/bin/app.pid.service"
        runtime.mkdir()
        backup = root / "var/service-manager/reset-backups"
        backup.parent.mkdir(parents=True)
        backup.symlink_to(outside, target_is_directory=True)
        result = run("bash", root / "tools/script/reset-service-state.sh", "--force", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(runtime.exists())

    def test_parallel_systemd_start_and_changed_unit_leave_no_reload_warning(self):
        root = self.package()
        manager = root / "tools/script/service-manager.sh"
        env = dict(os.environ, ATF_SERVICE_MANAGER="systemd")
        states = [root / f"runtime/{i}.service" for i in range(8)]
        def start(item):
            i, state = item
            return run("bash", manager, "start", state, f"maintenance-parallel-{i}", root, "--",
                       sys.executable, "-c", "import time; time.sleep(600)", env=env)
        try:
            for cycle in range(2):
                if cycle:
                    for state in states:
                        unit = (state / "unit").read_text().strip()
                        with (state / unit).open("a") as output:
                            output.write("\n# disk update before repeated start\n")
                with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                    results = list(pool.map(start, enumerate(states)))
                for result, state in zip(results, states):
                    self.assertNotIn("changed on disk", result.stderr)
                    unit = (state / "unit").read_text().strip()
                    self.assertEqual(run("systemctl", "--user", "show", "-p", "NeedDaemonReload",
                                         "--value", unit).stdout.strip(), "no")
        finally:
            for state in states:
                run("bash", manager, "kill", state)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0], *args], verbosity=2)
