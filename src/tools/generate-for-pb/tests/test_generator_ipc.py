import contextlib
import importlib
import io
import os
import sys
import tempfile
import types
import unittest
import uuid
from pathlib import Path
from unittest import mock

SCRIPT_DIR = Path(__file__).resolve().parents[1]
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import generator_ipc  # noqa: E402

TEST_SERVER_ADDRESS = "127.0.0.1:39171"


class GeneratorIpcArgsTest(unittest.TestCase):

    def test_strip_generator_ipc_args_preserves_generation_args(self):
        argv = [
            "--client-mode",
            "--server-address",
            TEST_SERVER_ADDRESS,
            "--server-timeout=60",
            "--server-auto-start",
            "--server-pid-file",
            "pid.txt",
            "--keep-me",
            "value",
            "--server-shutdown",
        ]

        self.assertEqual([
            "--keep-me",
            "value",
        ], generator_ipc.strip_generator_ipc_args(argv))

    def test_collect_generator_server_bootstrap_args_is_empty(self):
        argv = [
            "--add-path",
            "path-a",
            "--add-package-prefix=prefix-a",
        ]

        self.assertEqual([], generator_ipc.collect_generator_server_bootstrap_args(argv))

    def test_configure_payload_round_trip(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            configure_path = Path(temp_dir) / "config.yaml"
            configure_path.write_text("key: value\n", encoding="utf-8")
            argv = ["--configure", "config.yaml", "--flag", "demo"]

            payload = generator_ipc._collect_configure_payload(argv, temp_dir)
            self.assertIsNotNone(payload)

            request = {"configure": payload}
            materialized_argv, materialized_path = generator_ipc._materialize_configure_payload(
                request,
                list(argv),
            )
            try:
                self.assertEqual([
                    "--configure",
                    materialized_path,
                    "--flag",
                    "demo",
                ], materialized_argv)
                self.assertEqual(configure_path.read_bytes(),
                                 Path(materialized_path).read_bytes())
            finally:
                if materialized_path and os.path.exists(materialized_path):
                    os.remove(materialized_path)


class GeneratorIpcClientTest(unittest.TestCase):

    def test_run_generator_client_shutdown_succeeds_when_server_pid_is_stale(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            pid_file = Path(temp_dir) / "server.pid"
            pid_file.write_text("12345\n", encoding="utf-8")
            stderr = io.StringIO()

            with mock.patch.object(generator_ipc,
                                   "_check_pid_file_process",
                                   return_value=(False, "server is not running")):
                with contextlib.redirect_stderr(stderr):
                    result = generator_ipc.run_generator_client(
                        TEST_SERVER_ADDRESS,
                        1,
                        [],
                        temp_dir,
                        "generator.py",
                        True,
                        auto_start=False,
                        idle_timeout=1,
                        server_program=None,
                        pid_file=str(pid_file),
                    )

            self.assertEqual(0, result)
            self.assertFalse(pid_file.exists())
            self.assertEqual("", stderr.getvalue())

    def test_run_generator_client_shutdown_succeeds_when_server_address_refuses(self):
        stderr = io.StringIO()
        with mock.patch.object(generator_ipc,
                               "_connect_and_request",
                               side_effect=ConnectionRefusedError("refused")):
            with contextlib.redirect_stderr(stderr):
                result = generator_ipc.run_generator_client(
                    TEST_SERVER_ADDRESS,
                    1,
                    [],
                    os.getcwd(),
                    "generator.py",
                    True,
                    auto_start=False,
                    idle_timeout=1,
                    server_program=None,
                    pid_file=None,
                )

        self.assertEqual(0, result)
        self.assertEqual("", stderr.getvalue())

    def test_run_generator_client_waits_for_existing_startup(self):
        with mock.patch.object(generator_ipc,
                               "_connect_and_request",
                               side_effect=[
                                   ConnectionRefusedError("refused"),
                                   {
                                       "returncode": 0,
                                       "stdout": "",
                                       "stderr": "",
                                   },
                               ]), \
                mock.patch.object(generator_ipc,
                                  "_acquire_generator_server_startup_lock",
                                  return_value=False) as acquire_lock, \
                mock.patch.object(generator_ipc,
                                  "_wait_generator_server_ready",
                                  return_value=True) as wait_ready, \
                mock.patch.object(generator_ipc,
                                  "_start_generator_server") as start_server:
            result = generator_ipc.run_generator_client(
                TEST_SERVER_ADDRESS,
                1,
                ["--demo"],
                os.getcwd(),
                "generator.py",
                False,
                auto_start=True,
                idle_timeout=1,
                server_program="generator.py",
                pid_file=None,
            )

        self.assertEqual(0, result)
        acquire_lock.assert_called_once()
        wait_ready.assert_called_once()
        start_server.assert_not_called()

    def test_start_generator_server_hides_windows_console(self):
        popen_result = object()

        class DummyStartupInfo(object):

            def __init__(self):
                self.dwFlags = 0
                self.wShowWindow = None

        with mock.patch.object(generator_ipc.os, "name", "nt"), \
                mock.patch.object(generator_ipc.subprocess,
                                  "Popen",
                                  return_value=popen_result) as popen_mock, \
                mock.patch.object(generator_ipc.subprocess,
                                  "CREATE_NEW_PROCESS_GROUP",
                                  0x200,
                                  create=True), \
                mock.patch.object(generator_ipc.subprocess,
                                  "DETACHED_PROCESS",
                                  0x008,
                                  create=True), \
                mock.patch.object(generator_ipc.subprocess,
                                  "CREATE_NO_WINDOW",
                                  0x08000000,
                                  create=True), \
                mock.patch.object(generator_ipc.subprocess,
                                  "STARTF_USESHOWWINDOW",
                                  0x1,
                                  create=True), \
                mock.patch.object(generator_ipc.subprocess,
                                  "SW_HIDE",
                                  0,
                                  create=True), \
                mock.patch.object(generator_ipc.subprocess,
                                  "STARTUPINFO",
                                  DummyStartupInfo,
                                  create=True):
            result = generator_ipc._start_generator_server(
                "generator.py",
                TEST_SERVER_ADDRESS,
                60,
                os.getcwd(),
                [],
                None,
            )

        self.assertIs(popen_result, result)
        popen_kwargs = popen_mock.call_args.kwargs
        self.assertEqual(0x200 | 0x008 | 0x08000000,
                         popen_kwargs["creationflags"])
        self.assertIsNotNone(popen_kwargs["startupinfo"])
        self.assertEqual(0x1, popen_kwargs["startupinfo"].dwFlags & 0x1)
        self.assertEqual(0, popen_kwargs["startupinfo"].wShowWindow)

    def test_get_subprocess_no_window_kwargs_on_windows(self):
        class DummyStartupInfo(object):

            def __init__(self):
                self.dwFlags = 0
                self.wShowWindow = None

        with mock.patch.object(generator_ipc.os, "name", "nt"), \
                mock.patch.object(generator_ipc.subprocess,
                                  "CREATE_NO_WINDOW",
                                  0x08000000,
                                  create=True), \
                mock.patch.object(generator_ipc.subprocess,
                                  "STARTF_USESHOWWINDOW",
                                  0x1,
                                  create=True), \
                mock.patch.object(generator_ipc.subprocess,
                                  "SW_HIDE",
                                  0,
                                  create=True), \
                mock.patch.object(generator_ipc.subprocess,
                                  "STARTUPINFO",
                                  DummyStartupInfo,
                                  create=True):
            kwargs = generator_ipc.get_subprocess_no_window_kwargs()

        self.assertEqual(0x08000000, kwargs["creationflags"])
        self.assertIsNotNone(kwargs["startupinfo"])
        self.assertEqual(0x1, kwargs["startupinfo"].dwFlags & 0x1)
        self.assertEqual(0, kwargs["startupinfo"].wShowWindow)

    def test_get_subprocess_no_window_kwargs_on_posix(self):
        with mock.patch.object(generator_ipc.os, "name", "posix"):
            self.assertEqual({},
                             generator_ipc.get_subprocess_no_window_kwargs())


class GeneratorIpcRuntimeIsolationTest(unittest.TestCase):

    def setUp(self):
        self._old_template_cache_dirs = set(
            generator_ipc._GENERATOR_IPC_TEMPLATE_CACHE_DIRS)

    def tearDown(self):
        generator_ipc._GENERATOR_IPC_TEMPLATE_CACHE_DIRS.clear()
        generator_ipc._GENERATOR_IPC_TEMPLATE_CACHE_DIRS.update(
            self._old_template_cache_dirs)

    def test_run_generation_request_restores_runtime_and_unloads_request_only_modules(self):
        baseline_module_name = f"baseline_keep_{uuid.uuid4().hex}"
        request_module_name = f"request_only_{uuid.uuid4().hex}"
        env_name = f"GENERATOR_IPC_TEST_{uuid.uuid4().hex}"

        original_cwd = os.getcwd()
        original_sys_path = list(sys.path)
        original_env_value = os.environ.get(env_name)

        with tempfile.TemporaryDirectory() as temp_dir:
            baseline_dir = Path(temp_dir) / "baseline"
            request_dir = Path(temp_dir) / "request"
            request_cwd = Path(temp_dir) / "cwd"
            baseline_dir.mkdir()
            request_dir.mkdir()
            request_cwd.mkdir()

            (baseline_dir / f"{baseline_module_name}.py").write_text(
                "VALUE = 'baseline'\n",
                encoding="utf-8",
            )
            (request_dir / f"{request_module_name}.py").write_text(
                "VALUE = 'request'\n",
                encoding="utf-8",
            )

            sys.path.insert(0, str(baseline_dir))

            def fake_main(argv=None, display_argv=None, allow_ipc=True):
                self.assertFalse(allow_ipc)
                self.assertEqual(str(request_cwd), os.getcwd())
                self.assertEqual([
                    "generator.py",
                    "--add-path",
                    str(request_dir),
                    "--demo",
                ], display_argv)
                self.assertNotIn(request_module_name, sys.modules)

                sys.path.insert(0, argv[argv.index("--add-path") + 1])
                importlib.import_module(baseline_module_name)
                importlib.import_module(request_module_name)
                importlib.import_module("argparse")

                os.environ[env_name] = "changed"
                print("stdout-from-main")
                sys.stderr.write("stderr-from-main\n")
                return 0

            request = {
                "action": "generate",
                "argv": ["--add-path", str(request_dir), "--demo"],
                "cwd": str(request_cwd),
                "display_argv": [
                    "generator.py",
                    "--add-path",
                    str(request_dir),
                    "--demo",
                ],
            }

            try:
                response = generator_ipc.run_generation_request(request,
                                                                fake_main)
                self.assertEqual(0, response["returncode"])
                self.assertIn("stdout-from-main", response["stdout"])
                self.assertIn("stderr-from-main", response["stderr"])

                self.assertEqual(original_cwd, os.getcwd())
                self.assertEqual([str(baseline_dir)] + original_sys_path,
                                 sys.path)
                self.assertEqual(original_env_value, os.environ.get(env_name))
                self.assertIn(baseline_module_name, sys.modules)
                self.assertNotIn(request_module_name, sys.modules)
                self.assertIn("argparse", sys.modules)
            finally:
                sys.path = original_sys_path
                sys.modules.pop(baseline_module_name, None)
                sys.modules.pop(request_module_name, None)
                if original_env_value is None:
                    os.environ.pop(env_name, None)
                else:
                    os.environ[env_name] = original_env_value

    def test_run_generation_request_keeps_always_reused_modules_loaded(self):
        retained_module_names = [
            f"google.protobuf.retained_{uuid.uuid4().hex}",
            f"yaml.retained_{uuid.uuid4().hex}",
            f"mako.retained_{uuid.uuid4().hex}",
            f"jinja2.retained_{uuid.uuid4().hex}",
            f"markupsafe.retained_{uuid.uuid4().hex}",
            f"_yaml.retained_{uuid.uuid4().hex}",
        ]

        def fake_main(argv=None, display_argv=None, allow_ipc=True):
            self.assertFalse(allow_ipc)
            for retained_module_name in retained_module_names:
                sys.modules[retained_module_name] = types.ModuleType(
                    retained_module_name)
            return 0

        def fake_main_check(argv=None, display_argv=None, allow_ipc=True):
            self.assertFalse(allow_ipc)
            for retained_module_name in retained_module_names:
                self.assertIn(retained_module_name, sys.modules)
            return 0

        try:
            first_response = generator_ipc.run_generation_request(
                {
                    "action": "generate",
                    "argv": [],
                    "cwd": os.getcwd(),
                    "display_argv": ["generator.py"],
                },
                fake_main,
            )
            self.assertEqual(0, first_response["returncode"])
            for retained_module_name in retained_module_names:
                self.assertIn(retained_module_name, sys.modules)

            second_response = generator_ipc.run_generation_request(
                {
                    "action": "generate",
                    "argv": [],
                    "cwd": os.getcwd(),
                    "display_argv": ["generator.py"],
                },
                fake_main_check,
            )
            self.assertEqual(0, second_response["returncode"])
            for retained_module_name in retained_module_names:
                self.assertIn(retained_module_name, sys.modules)
        finally:
            for retained_module_name in retained_module_names:
                sys.modules.pop(retained_module_name, None)

    def test_run_generation_request_keeps_registered_template_cache_modules_loaded(self):
        template_cache_module_name = f"template_cache_{uuid.uuid4().hex}"

        with tempfile.TemporaryDirectory() as temp_dir:
            cache_dir = Path(temp_dir) / "custom-template-cache"
            cache_dir.mkdir(parents=True)
            generated_module_path = cache_dir / "generated_module.py"
            generated_module_path.write_text("VALUE = 'template-cache'\n",
                                             encoding="utf-8")
            generator_ipc.register_generator_cache_dir(str(cache_dir))

            def fake_main(argv=None, display_argv=None, allow_ipc=True):
                self.assertFalse(allow_ipc)
                generated_module = types.ModuleType(template_cache_module_name)
                generated_module.__file__ = str(generated_module_path)
                sys.modules[template_cache_module_name] = generated_module
                return 0

            def fake_main_check(argv=None, display_argv=None, allow_ipc=True):
                self.assertFalse(allow_ipc)
                self.assertIn(template_cache_module_name, sys.modules)
                return 0

            try:
                first_response = generator_ipc.run_generation_request(
                    {
                        "action": "generate",
                        "argv": [],
                        "cwd": temp_dir,
                        "display_argv": ["generator.py"],
                    },
                    fake_main,
                )
                self.assertEqual(0, first_response["returncode"])
                self.assertIn(template_cache_module_name, sys.modules)

                second_response = generator_ipc.run_generation_request(
                    {
                        "action": "generate",
                        "argv": [],
                        "cwd": temp_dir,
                        "display_argv": ["generator.py"],
                    },
                    fake_main_check,
                )
                self.assertEqual(0, second_response["returncode"])
                self.assertIn(template_cache_module_name, sys.modules)
            finally:
                sys.modules.pop(template_cache_module_name, None)

    def test_run_generation_request_unloads_request_only_namespace_packages(self):
        namespace_name = f"request_namespace_{uuid.uuid4().hex}"
        module_name = f"submodule_{uuid.uuid4().hex}"
        full_module_name = f"{namespace_name}.{module_name}"

        with tempfile.TemporaryDirectory() as temp_dir:
            request_dir = Path(temp_dir) / "request"
            package_dir = request_dir / namespace_name
            package_dir.mkdir(parents=True)
            (package_dir / f"{module_name}.py").write_text(
                "VALUE = 'request-namespace'\n",
                encoding="utf-8",
            )

            def fake_main(argv=None, display_argv=None, allow_ipc=True):
                self.assertFalse(allow_ipc)
                sys.path.insert(0, argv[argv.index("--add-path") + 1])
                importlib.import_module(full_module_name)
                return 0

            response = generator_ipc.run_generation_request(
                {
                    "action": "generate",
                    "argv": ["--add-path", str(request_dir)],
                    "cwd": temp_dir,
                    "display_argv": [
                        "generator.py",
                        "--add-path",
                        str(request_dir),
                    ],
                },
                fake_main,
            )

            self.assertEqual(0, response["returncode"])
            self.assertNotIn(namespace_name, sys.modules)
            self.assertNotIn(full_module_name, sys.modules)

    def test_run_generation_request_handles_system_exit(self):
        def fake_main(argv=None, display_argv=None, allow_ipc=True):
            raise SystemExit(7)

        response = generator_ipc.run_generation_request(
            {
                "action": "generate",
                "argv": [],
                "cwd": os.getcwd(),
                "display_argv": ["generator.py"],
            },
            fake_main,
        )

        self.assertEqual(7, response["returncode"])


if __name__ == "__main__":
    unittest.main()
