#!/usr/bin/env python3
"""OpenObserve credential, initialization, lifecycle, and otelcol rendering acceptance."""
import argparse
import base64
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import yaml

parser = argparse.ArgumentParser()
parser.add_argument("--work-dir", required=True)
parser.add_argument("--atdtool")
parser.add_argument("--otelcol")
options, unittest_args = parser.parse_known_args()
ROOT = Path(options.work_dir).resolve()
ROOT.mkdir(parents=True, exist_ok=True)
SOURCE = Path(__file__).resolve().parents[1]
REPO = SOURCE.parents[2]
INITIALIZER = SOURCE / "openobserve/init_openobserve.py"


def run(*args, env=None, check=True, timeout=60):
    result = subprocess.run([str(arg) for arg in args], env=env, text=True, encoding="utf-8",
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    if check and result.returncode:
        raise AssertionError(f"{args}: exit {result.returncode}\n{result.stdout}")
    return result


def prepare(work, port=15080, organization="acceptance_org"):
    password_file = work / "openobserve-root-password.txt"
    values_file = work / "values/modules/telemetry.yaml"
    result = run(sys.executable, INITIALIZER, "prepare", "--password-file", password_file,
                 "--values-file", values_file, "--root-user-email", "admin@owent.net",
                 "--organization", organization, "--http-port", port)
    return password_file, values_file, result.stdout.strip().splitlines()[-1]


class ObserveHandler(BaseHTTPRequestHandler):
    def reply(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def authorized(self):
        return self.headers.get("Authorization") == self.server.state["token"]

    def do_GET(self):
        if self.path == "/healthz":
            if self.server.state["health_disconnects"] > 0:
                self.server.state["health_disconnects"] -= 1
                self.close_connection = True
                return
            if self.server.state["healthy"]:
                self.reply(200, {"status": "ok"})
            else:
                self.reply(503, {"status": "starting"})
            return
        if self.path == "/api/organizations":
            if not self.authorized():
                self.reply(401, {"message": "unauthorized"})
                return
            if self.server.state["organization_failures"] > 0:
                self.server.state["organization_failures"] -= 1
                self.reply(503, {"message": "initializing"})
                return
            self.reply(200, {"data": self.server.state["organizations"]})
            return
        self.reply(404, {"message": "not found"})

    def do_POST(self):
        if self.path != "/api/organizations":
            self.reply(404, {"message": "not found"})
            return
        if not self.authorized():
            self.reply(401, {"message": "unauthorized"})
            return
        length = int(self.headers.get("Content-Length", "0"))
        payload = json.loads(self.rfile.read(length))
        self.server.state["posts"].append(payload)
        identifier = ("generated-org-id" if self.server.state["server_assigned_organization_id"]
                      else payload["identifier"])
        organization = {"identifier": identifier, "name": payload["name"]}
        if not any(item["identifier"] == organization["identifier"]
                   for item in self.server.state["organizations"]):
            self.server.state["organizations"].append(organization)
        self.reply(200, {"organization": organization})

    def log_message(self, *_args):
        pass


def api_server():
    server = ThreadingHTTPServer(("127.0.0.1", 0), ObserveHandler)
    server.state = {"healthy": True, "health_disconnects": 0, "organization_failures": 0,
                    "server_assigned_organization_id": False, "organizations": [], "posts": [], "token": ""}
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server


def initialize(work, server, organization="acceptance_org", timeout=5, check=True, values_file=None):
    args = [sys.executable, INITIALIZER, "initialize",
            "--password-file", work / "openobserve-root-password.txt",
            "--root-user-email", "admin@owent.net", "--organization", organization,
            "--base-url", f"http://127.0.0.1:{server.server_address[1]}"]
    if values_file is not None:
        args.extend(("--values-file", values_file, "--http-port", server.server_address[1]))
    args.extend(("--startup-timeout", timeout))
    return run(*args, check=check)


FAKE_POSIX_ENGINE = r'''#!/usr/bin/env python3
import json
import os
from pathlib import Path
import signal
import sys
import time

args = sys.argv[1:]
action = args[0] if args else ""
running = Path(os.environ["FAKE_DOCKER_STATE"])
if action in ("info", "image", "pull", "rmi", "logs"):
    sys.exit(0)
if action == "container" and len(args) > 1 and args[1] == "inspect":
    args = ["inspect", *args[2:]]
    action = "inspect"
if action == "inspect":
    if len(args) >= 3 and args[1] == "-f":
        if args[2] == "{{.State.Running}}":
            print("true" if running.exists() else "false")
            sys.exit(0)
        if args[2] == "{{.Id}}":
            if running.exists():
                print("fake-container-id")
                sys.exit(0)
            sys.exit(1)
    sys.exit(0 if running.exists() else 1)
if action == "run":
    with open(os.environ["FAKE_DOCKER_LOG"], "a", encoding="utf-8") as log:
        log.write(json.dumps(args) + "\n")
    running.touch()
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    while running.exists():
        time.sleep(0.05)
    sys.exit(0)
if action in ("stop", "kill", "rm"):
    running.unlink(missing_ok=True)
    sys.exit(0)
print("Unexpected fake engine arguments: " + repr(args), file=sys.stderr)
sys.exit(2)
'''


FAKE_ENGINE = r'''param([Parameter(ValueFromRemainingArguments = $true)][string[]]$EngineArgs)
$action = $EngineArgs[0]
$running = $env:FAKE_DOCKER_STATE
if ($action -eq 'info' -or $action -eq 'pull' -or $action -eq 'rmi' -or $action -eq 'logs') { exit 0 }
if ($action -eq 'image') { exit 0 }
if ($action -eq 'inspect') {
  if ($EngineArgs.Count -ge 3 -and $EngineArgs[1] -eq '-f') {
    if ($EngineArgs[2] -eq '{{.State.Running}}') {
      if (Test-Path -LiteralPath $running) { Write-Output 'true' } else { Write-Output 'false' }
      exit 0
    }
    if ($EngineArgs[2] -eq '{{.Id}}') {
      if (Test-Path -LiteralPath $running) { Write-Output 'fake-container-id'; exit 0 }
      exit 1
    }
  }
  if (Test-Path -LiteralPath $running) { exit 0 }
  exit 1
}
if ($action -eq 'run') {
  ($EngineArgs | ConvertTo-Json -Depth 4 -Compress) | Add-Content -LiteralPath $env:FAKE_DOCKER_LOG
  New-Item -Path $running -ItemType File -Force | Out-Null
  Write-Output 'fake-container-id'
  exit 0
}
if ($action -eq 'stop' -or $action -eq 'rm') {
  Remove-Item -LiteralPath $running -Force -ErrorAction SilentlyContinue
  exit 0
}
Write-Error "Unexpected fake engine arguments: $EngineArgs"
exit 2
'''


class OpenObserveAcceptance(unittest.TestCase):
    def test_prepare_reuses_password_and_writes_scoped_values(self):
        work = ROOT / "prepare"
        password_file, values_file, first = prepare(work)
        self.assertGreaterEqual(len(first), 40)
        for pattern in (r"[a-z]", r"[A-Z]", r"[0-9]", r"[^A-Za-z0-9]"):
            self.assertRegex(first, pattern)
        second = prepare(work)[2]
        self.assertEqual(second, first)
        self.assertEqual(password_file.read_text().strip(), first)
        values = yaml.safe_load(values_file.read_text())
        self.assertNotIn("telemetry", values)  # atdtool supplies this key from modules/telemetry.yaml.
        token = "Basic " + base64.b64encode(f"admin@owent.net:{first}".encode()).decode()
        http_api = values["opentelemetry"]["metrics"]["prometheus"]["http_api"]
        self.assertEqual(http_api["url"], "http://127.0.0.1:15080/api/acceptance_org/prometheus/api/v1")
        self.assertEqual(http_api["headers"], [{"key": "Authorization", "value": token}])
        exporters = values["agent"]["otelcol"]["exporters"]
        for signal, name, stream in (
                ("trace", "otlp_http/openobserve_trace", "atframework_traces"),
                ("metrics", "otlp_http/openobserve_metrics", "atframework_metrics"),
                ("logs", "otlp_http/openobserve_logs", "atframework_logs"),
                ("spanmetrics", "otlp_http/openobserve_spanmetrics", "atframework_metrics")):
            exporter = exporters[signal][name]
            self.assertEqual(exporter["endpoint"], "http://127.0.0.1:15080/api/acceptance_org")
            self.assertEqual(exporter["headers"],
                             {"Authorization": token, "stream-name": stream})

    @unittest.skipUnless(os.name == "posix", "POSIX file modes are required")
    def test_prepare_sets_private_posix_permissions(self):
        probe = ROOT / "posix-mode-probe"
        probe.touch()
        probe.chmod(0o600)
        if stat.S_IMODE(probe.stat().st_mode) != 0o600:
            self.skipTest("the test filesystem does not enforce POSIX mode bits (for example DrvFs)")
        password_file, values_file, _password = prepare(ROOT / "posix-permissions")
        self.assertEqual(stat.S_IMODE(password_file.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(values_file.stat().st_mode), 0o600)

    def test_initialize_is_idempotent_and_health_failure_does_not_create_org(self):
        work = ROOT / "initialize"
        prepare(work)
        server = api_server()
        try:
            password = (work / "openobserve-root-password.txt").read_text().strip()
            server.state["token"] = "Basic " + base64.b64encode(
                f"admin@owent.net:{password}".encode()).decode()
            server.state["organization_failures"] = 2
            initialize(work, server)
            initialize(work, server)
            self.assertEqual(len(server.state["posts"]), 1)
            self.assertEqual(server.state["posts"][0], {
                "name": "acceptance_org", "identifier": "acceptance_org", "org_type": "custom"})
            server.state["healthy"] = False
            failed = initialize(work, server, organization="must_not_be_created", timeout=0.2, check=False)
            self.assertNotEqual(failed.returncode, 0)
            self.assertFalse(any(post["identifier"] == "must_not_be_created"
                                 for post in server.state["posts"]))
        finally:
            server.shutdown()
            server.server_close()

    def test_initialize_retries_when_server_closes_early_connections(self):
        work = ROOT / "initialize-disconnects"
        prepare(work)
        server = api_server()
        try:
            password = (work / "openobserve-root-password.txt").read_text().strip()
            server.state["token"] = "Basic " + base64.b64encode(
                f"admin@owent.net:{password}".encode()).decode()
            server.state["health_disconnects"] = 2
            initialize(work, server, timeout=5)
            self.assertEqual(server.state["health_disconnects"], 0)
            self.assertEqual(len(server.state["posts"]), 1)
        finally:
            server.shutdown()
            server.server_close()

    def test_initialize_uses_server_assigned_organization_identifier_in_values(self):
        work = ROOT / "initialize-server-assigned-id"
        _password_file, values_file, password = prepare(work)
        server = api_server()
        try:
            server.state["token"] = "Basic " + base64.b64encode(
                f"admin@owent.net:{password}".encode()).decode()
            server.state["server_assigned_organization_id"] = True
            initialize(work, server, values_file=values_file)
            initialize(work, server, values_file=values_file)
            self.assertEqual(len(server.state["posts"]), 1)
            values = yaml.safe_load(values_file.read_text())
            self.assertEqual(values["opentelemetry"]["metrics"]["prometheus"]["http_api"]["url"],
                             f"http://127.0.0.1:{server.server_address[1]}/api/generated-org-id/prometheus/api/v1")
            exporters = values["agent"]["otelcol"]["exporters"]
            for signal in ("trace", "metrics", "logs", "spanmetrics"):
                exporter = next(iter(exporters[signal].values()))
                self.assertEqual(exporter["endpoint"],
                                 f"http://127.0.0.1:{server.server_address[1]}/api/generated-org-id")
        finally:
            server.shutdown()
            server.server_close()

    @unittest.skipUnless(options.atdtool and options.otelcol, "atdtool and otelcol paths are required")
    def test_rendered_otelcol_has_authenticated_signal_pipelines(self):
        work = ROOT / "render"
        _password_file, _values_file, password = prepare(work)
        values = ",".join((str(REPO / "install/cloud-native/values/default"),
                           str(REPO / "install/cloud-native/values/dev"),
                           str(REPO / "install/cloud-native/values/personal"),
                           str(work / "values")))
        run(options.atdtool, "template", REPO / "install/cloud-native/charts/otelcol",
            "-o", work / "rendered", "--values", values, "--mode", "nondeploy")
        config_path = work / "rendered/cfg/config.yaml"
        config = yaml.safe_load(config_path.read_text())
        token = "Basic " + base64.b64encode(f"admin@owent.net:{password}".encode()).decode()
        exporters = config["exporters"]
        for name, stream in (("otlp_http/openobserve_trace", "atframework_traces"),
                             ("otlp_http/openobserve_metrics", "atframework_metrics"),
                             ("otlp_http/openobserve_logs", "atframework_logs"),
                             ("otlp_http/openobserve_spanmetrics", "atframework_metrics")):
            self.assertEqual(exporters[name]["endpoint"], "http://127.0.0.1:15080/api/acceptance_org")
            self.assertEqual(exporters[name]["headers"],
                             {"Authorization": token, "stream-name": stream})
            self.assertEqual(exporters[name]["sending_queue"]["queue_size"], 4096)
        pipelines = config["service"]["pipelines"]
        self.assertIn("otlp_http/openobserve_trace", pipelines["traces/default"]["exporters"])
        self.assertIn("otlp_http/openobserve_metrics", pipelines["metrics/default"]["exporters"])
        self.assertIn("otlp_http/openobserve_spanmetrics",
                      pipelines["metrics/spanmetrics_default_default"]["exporters"])
        for name in ("logs/default", "logs/cs_actor", "logs/notification"):
            self.assertIn("otlp_http/openobserve_logs", pipelines[name]["exporters"])
            self.assertEqual(pipelines[name]["processors"][0], f"memory_limiter/{name.split('/')[1]}")
        self.assertIn("span_metrics/default_default", pipelines["traces/default"]["exporters"])
        self.assertEqual(exporters["file/rotation_metrics_default"]["path"],
                         "../log/otelcol-metrics-default.log")
        run(options.otelcol, "validate", f"--config={config_path}")

    @unittest.skipUnless(os.name == "posix" and shutil.which("bash"), "Bash is required")
    def test_generate_config_loads_script_local_values(self):
        script_root = ROOT / "generate-config/tools/script"
        shutil.copytree(SOURCE, script_root, dirs_exist_ok=True)
        (script_root / "update_dependency.sh").write_text("#!/bin/bash\nexit 0\n")
        atdtool = script_root.parent / "atdtool/atdtool"
        atdtool.parent.mkdir(parents=True, exist_ok=True)
        atdtool.write_text('#!/bin/sh\nprintf \'%s\\n\' "$*" >> "$ATDTOOL_LOG"\n')
        atdtool.chmod(0o755)
        values_file = script_root / "var/local-test-env/openobserve/values/modules/telemetry.yaml"
        values_file.parent.mkdir(parents=True, exist_ok=True)
        values_file.write_text("{}\n")
        log = ROOT / "generate-config-atdtool.log"
        run("bash", script_root / "generate_config.sh", env=dict(os.environ, ATDTOOL_LOG=str(log)))
        calls = log.read_text(encoding="utf-8").splitlines()
        self.assertEqual(len(calls), 4)
        for call in calls:
            self.assertIn(",var/local-test-env/openobserve/values", call)

    @unittest.skipUnless(os.name == "posix" and shutil.which("sh") and shutil.which("bash"),
                         "POSIX shell and Bash are required")
    def test_posix_lifecycle_uses_service_manager_and_preserves_data(self):
        engine = ROOT / "fake-docker"
        engine.write_text(FAKE_POSIX_ENGINE, encoding="utf-8")
        engine.chmod(0o755)
        state = ROOT / "fake-docker-running"
        log = ROOT / "fake-docker-log.jsonl"
        env = dict(os.environ, FAKE_DOCKER_STATE=str(state), FAKE_DOCKER_LOG=str(log))
        script_root = ROOT / "posix-package/tools/script"
        shutil.copytree(SOURCE, script_root, dirs_exist_ok=True)
        work = script_root / "var/local-test-env/openobserve"
        script = script_root / "openobserve/openobserve.sh"
        server = api_server()
        try:
            def invoke(command):
                return run("sh", script, command,
                           "--http-port", server.server_address[1], "--container-engine", engine,
                           "--service-manager", "none", env=env, timeout=90)

            password_file, _values_file, password = prepare(work, server.server_address[1])
            server.state["token"] = "Basic " + base64.b64encode(
                f"admin@owent.net:{password}".encode()).decode()
            start = invoke("start")
            self.assertIn("OpenObserve is ready", start.stdout)
            self.assertTrue(state.exists())
            self.assertTrue((script_root / "var/service-manager/control").is_dir())
            self.assertFalse((ROOT / "posix-package/var/service-manager").exists())
            data_file = work / "data/keep.txt"
            data_file.write_text("persistent data", encoding="utf-8")
            invoke("stop")
            self.assertFalse(state.exists())
            self.assertEqual(data_file.read_text(encoding="utf-8"), "persistent data")
            self.assertEqual(password_file.read_text().strip(), password)
            invoke("start")
            self.assertEqual(password_file.read_text().strip(), password)
            self.assertEqual(len(server.state["posts"]), 1)
            invoke("stop")
            runs = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(runs), 2)
            for arguments in runs:
                joined = "\n".join(arguments)
                self.assertIn("ZO_ROOT_USER_EMAIL=admin@owent.net", joined)
                self.assertIn(f"ZO_ROOT_USER_PASSWORD={password}", joined)
                self.assertIn("ZO_COMPACT_DATA_RETENTION_DAYS=14", joined)
                self.assertIn("ZO_CREATE_ORG_THROUGH_INGESTION=false", joined)
                self.assertIn("--memory", arguments)
                self.assertIn("3g", arguments)
                self.assertIn("--cpus", arguments)
                self.assertIn("2", arguments)
                self.assertIn("public.ecr.aws/zinclabs/openobserve:v1.1.2", arguments)
                self.assertIn("/data", joined)
        finally:
            server.shutdown()
            server.server_close()
            run("bash", script_root / "service-manager.sh", "kill", work / "service",
                env=env, check=False)

    @unittest.skipUnless(shutil.which("pwsh"), "PowerShell 7 is required")
    def test_powershell_lifecycle_reuses_credentials_and_preserves_data(self):
        work = ROOT / "powershell-lifecycle"
        engine = ROOT / "fake-docker.ps1"
        engine.write_text(FAKE_ENGINE, encoding="utf-8")
        state = ROOT / "fake-docker-running"
        log = ROOT / "fake-docker-log.jsonl"
        env = dict(os.environ, FAKE_DOCKER_STATE=str(state), FAKE_DOCKER_LOG=str(log))
        script = SOURCE / "openobserve/openobserve.ps1"
        server = api_server()
        try:
            def invoke(command):
                return run(shutil.which("pwsh"), "-NoLogo", "-NoProfile", "-NonInteractive",
                           "-File", script, "-Command", command, "-WorkDir", work,
                           "-HttpPort", server.server_address[1], "-ContainerEngine", engine,
                           env=env, timeout=90)

            # Prepare once so the fake API can require the exact generated Basic token.
            password_file, _values_file, password = prepare(work, server.server_address[1])
            server.state["token"] = "Basic " + base64.b64encode(
                f"admin@owent.net:{password}".encode()).decode()
            start = invoke("start")
            self.assertIn("OpenObserve is ready", start.stdout)
            self.assertTrue(state.exists())
            data_file = work / "data/keep.txt"
            data_file.write_text("persistent data", encoding="utf-8")
            invoke("stop")
            self.assertFalse(state.exists())
            self.assertEqual(data_file.read_text(encoding="utf-8"), "persistent data")
            self.assertEqual(password_file.read_text().strip(), password)
            invoke("start")
            self.assertEqual(password_file.read_text().strip(), password)
            self.assertEqual(len(server.state["posts"]), 1)
            invoke("stop")
            runs = [json.loads(line) for line in log.read_text(encoding="utf-8").splitlines()]
            self.assertEqual(len(runs), 2)
            for arguments in runs:
                joined = "\n".join(arguments)
                self.assertIn("ZO_ROOT_USER_EMAIL=admin@owent.net", joined)
                self.assertIn(f"ZO_ROOT_USER_PASSWORD={password}", joined)
                self.assertIn("ZO_COMPACT_DATA_RETENTION_DAYS=14", joined)
                self.assertIn("ZO_CREATE_ORG_THROUGH_INGESTION=false", joined)
                self.assertIn("--memory", arguments)
                self.assertIn("3g", arguments)
                self.assertIn("--cpus", arguments)
                self.assertIn("2", arguments)
                self.assertIn("public.ecr.aws/zinclabs/openobserve:v1.1.2", arguments)
                self.assertIn("/data", joined)
        finally:
            server.shutdown()
            server.server_close()
            if state.exists():
                run(shutil.which("pwsh"), "-NoLogo", "-NoProfile", "-NonInteractive",
                    "-File", script, "-Command", "stop", "-WorkDir", work,
                    "-ContainerEngine", engine, env=env, check=False)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0], *unittest_args], verbosity=2)
