import contextlib
import importlib.util
import io
import json
import os
import socket
import sys
import tempfile
import threading
import types
import unittest
import uuid
from pathlib import Path
from unittest import mock

SCRIPT_DIR = Path(__file__).resolve().parents[1]
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import generator_ipc  # noqa: E402

GENERATOR_SCRIPTS = {
    "mako": SCRIPT_DIR / "mako-generator.py",
    "jinja2": SCRIPT_DIR / "jinja2-generator.py",
}


def load_generator_module(file_path):
    module_name = f"generate_for_pb_test_{file_path.stem.replace('-', '_')}_{uuid.uuid4().hex}"
    spec = importlib.util.spec_from_file_location(module_name, file_path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = module
    spec.loader.exec_module(module)
    return module_name, module


class GeneratorScriptRuntimeTest(unittest.TestCase):

    def setUp(self):
        self._old_template_cache_dirs = set(
            generator_ipc._GENERATOR_IPC_TEMPLATE_CACHE_DIRS)

    def tearDown(self):
        generator_ipc._GENERATOR_IPC_TEMPLATE_CACHE_DIRS.clear()
        generator_ipc._GENERATOR_IPC_TEMPLATE_CACHE_DIRS.update(
            self._old_template_cache_dirs)

    def _load_module(self, key):
        module_name, module = load_generator_module(GENERATOR_SCRIPTS[key])
        self.addCleanup(sys.modules.pop, module_name, None)
        return module

    def _make_package_prefix(self, temp_dir):
        prefix_dir = Path(temp_dir) / "python-prefix"
        (prefix_dir / "Scripts").mkdir(parents=True)
        (prefix_dir / "Lib" / "site-packages").mkdir(parents=True)
        return prefix_dir

    def _make_server_address(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.bind(("127.0.0.1", 0))
            host, port = sock.getsockname()
        return "{0}:{1}".format(host, port)

    def _install_fake_print_color_module(self):
        print_color_module = types.ModuleType("print_color")
        print_color_module.print_style = types.SimpleNamespace(
            FC_RED=1,
            FC_GREEN=2,
            FC_YELLOW=3,
            FW_BOLD=4,
        )
        print_color_module.cprintf_stdout = lambda *args, **kwargs: None
        print_color_module.cprintf_stderr = lambda *args, **kwargs: None
        return print_color_module

    def _install_fake_yaml_module(self):
        yaml_module = types.ModuleType("yaml")
        yaml_module.SafeLoader = object()
        yaml_module.load = lambda content, Loader=None: json.loads(content)
        return yaml_module

    def _install_fake_template_modules(self, key, compiled_module_names,
                                       compile_counter):
        def ensure_compiled(cache_dir, template_name):
            compiled_module_name = "generate_for_pb_template_cache_{0}".format(
                uuid.uuid5(
                    uuid.NAMESPACE_URL,
                    "{0}:{1}:{2}".format(
                        key,
                        os.path.realpath(cache_dir),
                        template_name,
                    ),
                ).hex)
            compiled_module = sys.modules.get(compiled_module_name)
            if compiled_module is None:
                compile_counter["count"] += 1
                compiled_module = types.ModuleType(compiled_module_name)
                compiled_module.__file__ = os.path.join(
                    cache_dir,
                    "{0}.py".format(template_name),
                )
                sys.modules[compiled_module_name] = compiled_module
                compiled_module_names.add(compiled_module_name)
            return compiled_module

        if key == "mako":
            mako_module = types.ModuleType("mako")
            mako_module.__path__ = []
            mako_template_module = types.ModuleType("mako.template")
            mako_lookup_module = types.ModuleType("mako.lookup")

            class FakeRuleTemplate(object):

                def __init__(self, content, lookup=None):
                    self.content = content

                def render(self, **kwargs):
                    return self.content

            class FakeSourceTemplate(object):

                def __init__(self, template_name, module_directory):
                    self.template_name = template_name
                    self.module_directory = module_directory

                def render(self, **kwargs):
                    ensure_compiled(self.module_directory, self.template_name)
                    return "rendered:{0}\n".format(self.template_name)

            class FakeTemplateLookup(object):

                def __init__(self, directories, module_directory):
                    self.module_directory = module_directory

                def get_template(self, template_name):
                    return FakeSourceTemplate(template_name,
                                              self.module_directory)

            mako_template_module.Template = FakeRuleTemplate
            mako_lookup_module.TemplateLookup = FakeTemplateLookup
            mako_module.template = mako_template_module
            mako_module.lookup = mako_lookup_module
            return {
                "mako": mako_module,
                "mako.template": mako_template_module,
                "mako.lookup": mako_lookup_module,
            }

        jinja2_module = types.ModuleType("jinja2")

        class FakeFileSystemBytecodeCache(object):

            def __init__(self, directory):
                self.directory = directory

        class FakeFileSystemLoader(object):

            def __init__(self, directories):
                self.directories = directories

        class FakeLiteralTemplate(object):

            def __init__(self, content):
                self.content = content

            def render(self, **kwargs):
                return self.content

        class FakeSourceTemplate(object):

            def __init__(self, template_name, cache_dir):
                self.template_name = template_name
                self.cache_dir = cache_dir

            def render(self, **kwargs):
                ensure_compiled(self.cache_dir, self.template_name)
                return "rendered:{0}\n".format(self.template_name)

        class FakeEnvironment(object):

            def __init__(self,
                         bytecode_cache=None,
                         loader=None,
                         autoescape=None,
                         keep_trailing_newline=True):
                self.bytecode_cache = bytecode_cache

            def from_string(self, content):
                return FakeLiteralTemplate(content)

            def get_template(self, template_name):
                return FakeSourceTemplate(template_name,
                                          self.bytecode_cache.directory)

        jinja2_module.Environment = FakeEnvironment
        jinja2_module.FileSystemLoader = FakeFileSystemLoader
        jinja2_module.FileSystemBytecodeCache = FakeFileSystemBytecodeCache
        jinja2_module.select_autoescape = lambda *args, **kwargs: None
        return {"jinja2": jinja2_module}

    def test_prepend_unique_paths_deduplicates_equivalent_paths(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            first_path = Path(temp_dir) / "first"
            second_path = Path(temp_dir) / "second"
            first_path.mkdir()
            second_path.mkdir()

            for key in GENERATOR_SCRIPTS:
                module = self._load_module(key)
                with self.subTest(generator=key):
                    result = module._prepend_unique_paths(
                        [
                            str(first_path),
                            str(first_path / "."),
                            str(second_path),
                        ],
                        [
                            str(second_path),
                            str(first_path),
                        ],
                    )
                    self.assertEqual([
                        str(first_path),
                        str(second_path),
                    ], result)

    def test_collect_package_prefix_python_paths_deduplicates_paths(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            prefix_dir = self._make_package_prefix(temp_dir)
            expected_prefix = os.path.realpath(prefix_dir)
            expected_scripts = os.path.realpath(prefix_dir / "Scripts")
            expected_site_packages = os.path.realpath(prefix_dir / "Lib" / "site-packages")

            for key in GENERATOR_SCRIPTS:
                module = self._load_module(key)
                with self.subTest(generator=key):
                    bin_paths, lib_paths = module._collect_package_prefix_python_paths(
                        [str(prefix_dir), str(prefix_dir)])
                    self.assertEqual([expected_scripts], bin_paths)
                    self.assertEqual([
                        expected_prefix,
                        expected_site_packages,
                    ], lib_paths)

    def test_pb_db_cache_tracks_protobuf_runtime_signature(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            pb_file = Path(temp_dir) / "demo.pb"
            external_pb_file = Path(temp_dir) / "external.pb"
            pb_file.write_bytes(b"demo-pb")
            external_pb_file.write_bytes(b"external-pb")

            for key in GENERATOR_SCRIPTS:
                module = self._load_module(key)
                load_calls = []

                class DummyPbDatabase(object):

                    def load(self, pb_file_path, external_pb_files):
                        load_calls.append((pb_file_path,
                                           tuple(external_pb_files)))

                with self.subTest(generator=key):
                    with mock.patch.object(module, "LOCAL_PB_DB_CACHE", {}), \
                            mock.patch.object(module, "PbDatabase",
                                              DummyPbDatabase), \
                            mock.patch.object(
                                module,
                                "get_protobuf_runtime_cache_signature",
                                side_effect=[("sig-1",), ("sig-1",),
                                             ("sig-2",)],
                            ):
                        first = module.get_pb_db_with_cache(
                            str(pb_file),
                            [str(external_pb_file)],
                        )
                        second = module.get_pb_db_with_cache(
                            str(pb_file),
                            [str(external_pb_file)],
                        )
                        third = module.get_pb_db_with_cache(
                            str(pb_file),
                            [str(external_pb_file)],
                        )

                    self.assertIs(first, second)
                    self.assertIsNot(first, third)
                    self.assertEqual(2, len(load_calls))
                    for loaded_pb_path, loaded_external_pb_files in load_calls:
                        self.assertEqual(os.path.realpath(pb_file),
                                         loaded_pb_path)
                        self.assertEqual(1, len(loaded_external_pb_files))
                        self.assertTrue(
                            os.path.samefile(external_pb_file,
                                             loaded_external_pb_files[0]))

    def test_client_mode_does_not_mutate_runtime_paths(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            add_path = Path(temp_dir) / "add-path"
            add_path.mkdir()
            prefix_dir = self._make_package_prefix(temp_dir)

            for key in GENERATOR_SCRIPTS:
                module = self._load_module(key)
                original_sys_path = list(sys.path)
                original_path_env = os.environ.get("PATH")
                captured = {}

                def fake_run_generator_client(address, timeout, argv, cwd,
                                              display_argv0, shutdown,
                                              auto_start, idle_timeout,
                                              server_program, pid_file):
                    captured["address"] = address
                    captured["argv"] = list(argv)
                    captured["cwd"] = cwd
                    captured["display_argv0"] = display_argv0
                    captured["shutdown"] = shutdown
                    captured["auto_start"] = auto_start
                    captured["idle_timeout"] = idle_timeout
                    captured["server_program"] = server_program
                    captured["pid_file"] = pid_file
                    return 321

                with self.subTest(generator=key):
                    with mock.patch.object(module.generator_ipc,
                                           "run_generator_client",
                                           side_effect=fake_run_generator_client):
                        result = module.main(
                            argv=[
                                "--client-mode",
                                "--server-address",
                                "127.0.0.1:3811",
                                "--server-pid-file",
                                "client.pid",
                                "--add-path",
                                str(add_path),
                                "--add-path",
                                str(add_path),
                                "--add-package-prefix",
                                str(prefix_dir),
                                "--add-package-prefix",
                                str(prefix_dir),
                                "--print-output-files",
                            ],
                            display_argv=["generator.py"],
                            allow_ipc=True,
                        )

                    self.assertEqual(321, result)
                    self.assertEqual(original_sys_path, sys.path)
                    self.assertEqual(original_path_env, os.environ.get("PATH"))
                    self.assertEqual([
                        "--add-path",
                        str(add_path),
                        "--add-path",
                        str(add_path),
                        "--add-package-prefix",
                        str(prefix_dir),
                        "--add-package-prefix",
                        str(prefix_dir),
                        "--print-output-files",
                    ], captured["argv"])

    def test_server_mode_applies_paths_before_launching_server(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            add_path = Path(temp_dir) / "add-path"
            add_path.mkdir()
            prefix_dir = self._make_package_prefix(temp_dir)
            expected_scripts = os.path.realpath(prefix_dir / "Scripts")
            expected_prefix = os.path.realpath(prefix_dir)
            expected_site_packages = os.path.realpath(prefix_dir / "Lib" / "site-packages")

            for key in GENERATOR_SCRIPTS:
                module = self._load_module(key)
                original_sys_path = list(sys.path)
                original_path_env = os.environ.get("PATH")
                captured = {}

                def fake_run_generator_server(address, idle_timeout,
                                              request_callback, pid_file=None):
                    captured["address"] = address
                    captured["idle_timeout"] = idle_timeout
                    captured["pid_file"] = pid_file
                    captured["sys_path"] = list(sys.path)
                    captured["path_env"] = os.environ.get("PATH", "")
                    captured["request_callback"] = request_callback
                    return 654

                try:
                    with self.subTest(generator=key):
                        with mock.patch.object(module.generator_ipc,
                                               "run_generator_server",
                                               side_effect=fake_run_generator_server):
                            result = module.main(
                                argv=[
                                    "--server-mode",
                                    "--server-address",
                                    "127.0.0.1:3812",
                                    "--server-pid-file",
                                    "server.pid",
                                    "--add-path",
                                    str(add_path),
                                    "--add-path",
                                    str(add_path),
                                    "--add-package-prefix",
                                    str(prefix_dir),
                                    "--add-package-prefix",
                                    str(prefix_dir),
                                ],
                                display_argv=["generator.py"],
                                allow_ipc=True,
                            )

                        self.assertEqual(654, result)
                        normalized_sys_path = [
                            module._normalize_path_for_compare(path)
                            for path in captured["sys_path"]
                        ]
                        self.assertEqual(1, normalized_sys_path.count(
                            module._normalize_path_for_compare(str(add_path))))
                        self.assertEqual(1, normalized_sys_path.count(
                            module._normalize_path_for_compare(expected_prefix)))
                        self.assertEqual(1, normalized_sys_path.count(
                            module._normalize_path_for_compare(expected_site_packages)))
                        normalized_path_env = [
                            module._normalize_path_for_compare(path)
                            for path in captured["path_env"].split(os.pathsep)
                            if path
                        ]
                        self.assertTrue(normalized_path_env)
                        self.assertEqual(
                            module._normalize_path_for_compare(expected_scripts),
                            normalized_path_env[0],
                        )
                        self.assertTrue(callable(captured["request_callback"]))
                finally:
                    sys.path = original_sys_path
                    if original_path_env is None:
                        os.environ.pop("PATH", None)
                    else:
                        os.environ["PATH"] = original_path_env

    def test_allow_ipc_false_rejects_ipc_options(self):
        for key in GENERATOR_SCRIPTS:
            module = self._load_module(key)
            stderr = io.StringIO()
            with self.subTest(generator=key):
                with contextlib.redirect_stderr(stderr):
                    result = module.main(
                        argv=["--client-mode"],
                        display_argv=["generator.py"],
                        allow_ipc=False,
                    )
                self.assertEqual(1, result)
                self.assertIn("not allowed in server requests",
                              stderr.getvalue())

    def test_client_requests_reuse_pb_and_template_caches_on_same_server(self):
        for key in GENERATOR_SCRIPTS:
            module = self._load_module(key)
            compiled_module_names = set()
            compile_counter = {"count": 0}
            pb_load_counter = {"count": 0}

            with tempfile.TemporaryDirectory() as temp_dir:
                temp_dir_path = Path(temp_dir)
                pb_file = temp_dir_path / "input.pb"
                pb_file.write_bytes(b"dummy-pb")
                template_suffix = ".mako" if key == "mako" else ".jinja2"
                template_file = temp_dir_path / ("template" + template_suffix)
                template_file.write_text("template-body\n", encoding="utf-8")
                output_dir = temp_dir_path / "output"
                output_dir.mkdir()
                output_file = output_dir / "generated.txt"
                config_file = temp_dir_path / "config.yaml"
                config_file.write_text(
                    json.dumps({
                        "configure": {
                            "encoding": "utf-8",
                            "output_directory": str(output_dir),
                            "overwrite": True,
                            "protocol_input_pb_file": str(pb_file),
                            "protocol_project_directory": str(temp_dir_path),
                        },
                        "rules": [
                            {
                                "global": {
                                    "overwrite": True,
                                    "input": str(template_file),
                                    "output": output_file.name,
                                }
                            }
                        ],
                    }),
                    encoding="utf-8",
                )

                fake_modules = {
                    "print_color": self._install_fake_print_color_module(),
                    "yaml": self._install_fake_yaml_module(),
                }
                fake_modules.update(
                    self._install_fake_template_modules(
                        key,
                        compiled_module_names,
                        compile_counter,
                    ))

                class DummyPbDatabase(object):

                    def load(self, pb_file_path, external_pb_files):
                        pb_load_counter["count"] += 1

                server_address = self._make_server_address()
                pid_file = temp_dir_path / "generator.server.pid"
                server_result = {}

                def fake_write_code_if_different(project_dir, output_file_path,
                                                 encoding, content,
                                                 clang_format_path,
                                                 clang_format_rule_re):
                    Path(output_file_path).parent.mkdir(parents=True,
                                                        exist_ok=True)
                    Path(output_file_path).write_text(content,
                                                      encoding=encoding)

                with self.subTest(generator=key), \
                        mock.patch.dict(sys.modules, fake_modules, clear=False), \
                        mock.patch.object(module,
                                          "check_has_module",
                                          return_value=True), \
                        mock.patch.object(module,
                                          "LOCAL_PB_DB_CACHE",
                                          {}), \
                        mock.patch.object(module,
                                          "PbDatabase",
                                          DummyPbDatabase), \
                        mock.patch.object(module,
                                          "get_protobuf_runtime_cache_signature",
                                          return_value=("test-runtime", )), \
                        mock.patch.object(module,
                                          "try_read_vcs_username",
                                          return_value="tester"), \
                        mock.patch.object(module,
                                          "write_code_if_different",
                                          side_effect=fake_write_code_if_different):
                    try:
                        server_thread = threading.Thread(
                            target=lambda: server_result.update({
                                "returncode": module.main(
                                    argv=[
                                        "--server-mode",
                                        "--server-address",
                                        server_address,
                                        "--server-pid-file",
                                        str(pid_file),
                                        "--server-idle-timeout",
                                        "30",
                                    ],
                                    display_argv=["generator.py"],
                                    allow_ipc=True,
                                )
                            }),
                            daemon=True,
                        )
                        server_thread.start()
                        generator_ipc._wait_generator_server_ready(
                            server_address,
                            5,
                            str(pid_file),
                            os.path.realpath(sys.executable),
                        )

                        for _ in range(2):
                            result = generator_ipc.run_generator_client(
                                server_address,
                                5,
                                ["--configure", str(config_file), "--quiet"],
                                temp_dir,
                                "generator.py",
                                False,
                                auto_start=False,
                                idle_timeout=30,
                                server_program=None,
                                pid_file=str(pid_file),
                            )
                            self.assertEqual(0, result)
                            self.assertTrue(server_thread.is_alive())
                            self.assertEqual(os.getpid(),
                                             generator_ipc._read_pid_file(
                                                 str(pid_file)))

                        self.assertEqual(1, pb_load_counter["count"])
                        self.assertEqual(1, compile_counter["count"])
                        self.assertTrue(output_file.exists())
                    finally:
                        shutdown_result = generator_ipc.run_generator_client(
                            server_address,
                            5,
                            [],
                            temp_dir,
                            "generator.py",
                            True,
                            auto_start=False,
                            idle_timeout=1,
                            server_program=None,
                            pid_file=str(pid_file),
                        )
                        self.assertEqual(0, shutdown_result)
                        server_thread.join(5)
                        self.assertFalse(server_thread.is_alive())
                        self.assertEqual(0, server_result.get("returncode"))

            for compiled_module_name in compiled_module_names:
                sys.modules.pop(compiled_module_name, None)


if __name__ == "__main__":
    unittest.main()
