#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Copyright (c) 2026 atframework

Local IPC helpers for generate-for-pb scripts.
"""

import base64
import contextlib
import io
import json
import os
import socket
import socketserver
import struct
import subprocess
import sys
import tempfile
import threading
import time
import traceback

GENERATOR_IPC_MAGIC = "generate-for-pb"
GENERATOR_IPC_VERSION = 1
GENERATOR_IPC_MAX_PAYLOAD_SIZE = 512 * 1024 * 1024
GENERATOR_IPC_DEFAULT_TIMEOUT = 300.0
GENERATOR_IPC_DEFAULT_IDLE_TIMEOUT = 600.0
_GENERATOR_IPC_LENGTH_STRUCT = struct.Struct("!I")


def add_generator_ipc_options(add_option, parser, default_address):
    add_option(
        parser,
        "--server-mode",
        action="store_true",
        help="run as a persistent generator server and cache protobuf ASTs",
        dest="server_mode",
        default=False,
    )
    add_option(
        parser,
        "--client-mode",
        action="store_true",
        help="send this generation request to a persistent generator server",
        dest="client_mode",
        default=False,
    )
    add_option(
        parser,
        "--server-address",
        action="store",
        help="generator server address, default: {0}".format(
            default_address),
        dest="server_address",
        default=default_address,
    )
    add_option(
        parser,
        "--server-timeout",
        action="store",
        help="generator server connect/read timeout in seconds",
        dest="server_timeout",
        default=GENERATOR_IPC_DEFAULT_TIMEOUT,
    )
    add_option(
        parser,
        "--server-idle-timeout",
        action="store",
        help="generator server idle shutdown timeout in seconds, default: 600",
        dest="server_idle_timeout",
        default=GENERATOR_IPC_DEFAULT_IDLE_TIMEOUT,
    )
    add_option(
        parser,
        "--server-pid-file",
        action="store",
        help="generator server pid file path",
        dest="server_pid_file",
        default=None,
    )
    add_option(
        parser,
        "--server-auto-start",
        action="store_true",
        help="auto start a generator server in client mode when needed",
        dest="server_auto_start",
        default=True,
    )
    add_option(
        parser,
        "--no-server-auto-start",
        action="store_false",
        help="do not auto start a generator server in client mode",
        dest="server_auto_start",
    )
    add_option(
        parser,
        "--server-shutdown",
        action="store_true",
        help="ask the generator server to stop, then exit",
        dest="server_shutdown",
        default=False,
    )


def parse_server_address(address):
    if not address:
        address = "127.0.0.1:0"
    if address.startswith("["):
        end_pos = address.rfind("]")
        if end_pos >= 0:
            host = address[1:end_pos]
            port = address[(end_pos + 1):]
            if port.startswith(":"):
                port = port[1:]
            return (host, int(port))

    if ":" in address:
        host, port = address.rsplit(":", 1)
    else:
        host, port = "127.0.0.1", address
    if not host:
        host = "127.0.0.1"
    return (host, int(port))


def normalize_return_code(return_code):
    if return_code is None:
        return 0
    try:
        return int(return_code)
    except Exception:
        return 1


def normalize_timeout(value, default_value):
    try:
        return float(value)
    except Exception:
        return float(default_value)


def normalize_pid_file_path(pid_file, cwd):
    if not pid_file:
        return None
    if os.path.isabs(pid_file):
        return os.path.realpath(pid_file)
    if not cwd:
        cwd = os.getcwd()
    return os.path.realpath(os.path.join(cwd, pid_file))


def _read_pid_file(pid_file):
    try:
        with open(pid_file, "r") as file_obj:
            content = file_obj.read().strip()
    except EnvironmentError:
        return None
    if not content:
        return None
    try:
        pid = int(content.split()[0])
    except Exception:
        return None
    if pid <= 0:
        return None
    return pid


def _replace_file(source_path, target_path):
    if hasattr(os, "replace"):
        os.replace(source_path, target_path)
        return
    if os.path.exists(target_path):
        os.remove(target_path)
    os.rename(source_path, target_path)


def _write_pid_file(pid_file, pid):
    if not pid_file:
        return
    pid_file_dir = os.path.dirname(os.path.abspath(pid_file))
    if pid_file_dir and not os.path.exists(pid_file_dir):
        try:
            os.makedirs(pid_file_dir)
        except EnvironmentError:
            if not os.path.isdir(pid_file_dir):
                raise
    tmp_pid_file = "{0}.tmp.{1}".format(pid_file, os.getpid())
    with open(tmp_pid_file, "w") as file_obj:
        file_obj.write("{0}\n".format(pid))
    _replace_file(tmp_pid_file, pid_file)


def _remove_pid_file_if_match(pid_file, pid):
    if not pid_file:
        return
    if _read_pid_file(pid_file) != pid:
        return
    try:
        os.remove(pid_file)
    except EnvironmentError:
        pass


def _get_windows_process_image_path(pid):
    if os.name != "nt":
        return None
    try:
        import ctypes
        from ctypes import wintypes

        process_query_limited_information = 0x1000
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        open_process = kernel32.OpenProcess
        open_process.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        open_process.restype = wintypes.HANDLE
        query_full_process_image_name = kernel32.QueryFullProcessImageNameW
        query_full_process_image_name.argtypes = [
            wintypes.HANDLE,
            wintypes.DWORD,
            wintypes.LPWSTR,
            ctypes.POINTER(wintypes.DWORD),
        ]
        query_full_process_image_name.restype = wintypes.BOOL
        close_handle = kernel32.CloseHandle
        close_handle.argtypes = [wintypes.HANDLE]
        close_handle.restype = wintypes.BOOL

        process_handle = open_process(process_query_limited_information, False,
                                      int(pid))
        if not process_handle:
            return None
        try:
            buffer_size = wintypes.DWORD(32768)
            buffer = ctypes.create_unicode_buffer(buffer_size.value)
            if not query_full_process_image_name(process_handle, 0, buffer,
                                                 ctypes.byref(buffer_size)):
                return None
            return buffer.value
        finally:
            close_handle(process_handle)
    except BaseException:
        return None


def _get_posix_process_image_path(pid):
    if os.name == "nt":
        return None
    proc_exe = "/proc/{0}/exe".format(pid)
    try:
        if os.path.exists(proc_exe):
            return os.path.realpath(os.readlink(proc_exe))
    except BaseException:
        pass
    try:
        output = subprocess.check_output(
            ["ps", "-p", str(pid), "-o", "comm="],
            stderr=subprocess.DEVNULL,
        )
        process_path = output.decode("utf-8", errors="replace").strip()
        if process_path:
            return process_path
    except BaseException:
        return None
    return None


def _get_process_image_path(pid):
    if int(pid) == os.getpid():
        return os.path.realpath(sys.executable)
    if os.name == "nt":
        return _get_windows_process_image_path(pid)
    return _get_posix_process_image_path(pid)


def _normalize_process_image_path(process_path):
    if not process_path:
        return None
    return os.path.normcase(os.path.realpath(os.path.abspath(process_path)))


def _check_pid_file_process(pid_file, expected_process_path):
    pid = _read_pid_file(pid_file)
    if pid is None:
        return (False, "pid file {0} is missing or invalid".format(pid_file))
    process_path = _get_process_image_path(pid)
    if not process_path:
        return (False, "pid {0} from {1} is not running".format(
            pid, pid_file))
    normalized_process_path = _normalize_process_image_path(process_path)
    normalized_expected_path = _normalize_process_image_path(
        expected_process_path)
    if normalized_expected_path and normalized_process_path != normalized_expected_path:
        return (
            False,
            "pid {0} process path mismatch: {1} != {2}".format(
                pid, process_path, expected_process_path),
        )
    return (True, None)


def _check_server_ping_response(response, pid_file, expected_process_path):
    if not pid_file:
        return (True, None)
    pid = _read_pid_file(pid_file)
    response_pid = response.get("pid")
    try:
        response_pid = int(response_pid)
    except Exception:
        return (False, "generator server ping response has no pid")
    if response_pid != pid:
        return (
            False,
            "generator server pid mismatch: ping={0}, pid-file={1}".format(
                response_pid, pid),
        )
    response_process_path = response.get("process_path")
    normalized_response_path = _normalize_process_image_path(
        response_process_path)
    normalized_expected_path = _normalize_process_image_path(
        expected_process_path)
    if normalized_expected_path and normalized_response_path != normalized_expected_path:
        return (
            False,
            "generator server process path mismatch: {0} != {1}".format(
                response_process_path, expected_process_path),
        )
    return (True, None)


def _ping_generator_server(address, connect_timeout, read_timeout, pid_file,
                           expected_process_path):
    request = {
        "magic": GENERATOR_IPC_MAGIC,
        "version": GENERATOR_IPC_VERSION,
        "action": "ping",
    }
    response = _connect_and_request(address, connect_timeout, read_timeout,
                                    request)
    if normalize_return_code(response.get("returncode", 1)) != 0:
        raise RuntimeError(response.get("stderr", "ping failed"))
    ping_ready, ping_error = _check_server_ping_response(
        response, pid_file, expected_process_path)
    if not ping_ready:
        raise RuntimeError(ping_error)
    return response


def strip_generator_ipc_args(argv):
    value_options = set(
        [
            "--server-address",
            "--server-timeout",
            "--server-idle-timeout",
            "--server-pid-file",
        ])
    flag_options = set(
        [
            "--server-mode",
            "--client-mode",
            "--server-auto-start",
            "--no-server-auto-start",
            "--server-shutdown",
        ])
    ret = []
    skip_next = False
    for arg in argv:
        if skip_next:
            skip_next = False
            continue
        option_name = arg.split("=", 1)[0]
        if option_name in flag_options:
            continue
        if option_name in value_options:
            if "=" not in arg:
                skip_next = True
            continue
        ret.append(arg)
    return ret


def collect_generator_server_bootstrap_args(argv):
    value_options = set(["--add-path", "--add-package-prefix"])
    ret = []
    index = 0
    while index < len(argv):
        arg = argv[index]
        option_name = arg.split("=", 1)[0]
        if option_name in value_options:
            ret.append(arg)
            if "=" not in arg:
                index += 1
                if index < len(argv):
                    ret.append(argv[index])
            index += 1
            continue
        index += 1
    return ret


def _recv_exact(sock, size):
    chunks = []
    left_size = size
    while left_size > 0:
        chunk = sock.recv(left_size)
        if not chunk:
            raise EOFError("connection closed while reading payload")
        chunks.append(chunk)
        left_size -= len(chunk)
    return b"".join(chunks)


def _recv_json(sock):
    header = _recv_exact(sock, _GENERATOR_IPC_LENGTH_STRUCT.size)
    payload_size = _GENERATOR_IPC_LENGTH_STRUCT.unpack(header)[0]
    if payload_size > GENERATOR_IPC_MAX_PAYLOAD_SIZE:
        raise ValueError("IPC payload is too large: {0}".format(payload_size))
    payload = _recv_exact(sock, payload_size)
    return json.loads(payload.decode("utf-8"))


def _send_json(sock, payload):
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    if len(data) > GENERATOR_IPC_MAX_PAYLOAD_SIZE:
        raise ValueError("IPC payload is too large: {0}".format(len(data)))
    sock.sendall(_GENERATOR_IPC_LENGTH_STRUCT.pack(len(data)))
    sock.sendall(data)


def _find_configure_arg(argv):
    ret = None
    for index, arg in enumerate(argv):
        if arg in ("-c", "--configure"):
            if index + 1 < len(argv):
                ret = (index, index + 1, argv[index + 1])
            continue
        if arg.startswith("--configure="):
            ret = (index, None, arg.split("=", 1)[1])
            continue
        if arg.startswith("-c") and arg != "-c" and not arg.startswith("--"):
            ret = (index, None, arg[2:])
    return ret


def _collect_configure_payload(argv, cwd):
    configure_arg = _find_configure_arg(argv)
    if configure_arg is None:
        return None

    configure_path = configure_arg[2]
    if not configure_path:
        return None
    real_configure_path = configure_path
    if not os.path.isabs(real_configure_path):
        real_configure_path = os.path.join(cwd, real_configure_path)
    if not os.path.exists(real_configure_path):
        return None

    with open(real_configure_path, "rb") as file_obj:
        configure_content = file_obj.read()
    _, suffix = os.path.splitext(configure_path)
    if not suffix:
        suffix = ".yaml"
    return {
        "path": configure_path,
        "suffix": suffix,
        "content": base64.b64encode(configure_content).decode("ascii"),
    }


def _replace_configure_arg(argv, configure_path):
    ret = list(argv)
    configure_arg = _find_configure_arg(ret)
    if configure_arg is None:
        ret.extend(["--configure", configure_path])
        return ret

    option_index, value_index, _old_path = configure_arg
    if value_index is not None:
        ret[value_index] = configure_path
        return ret

    if ret[option_index].startswith("--configure="):
        ret[option_index] = "--configure={0}".format(configure_path)
    else:
        ret[option_index] = "-c{0}".format(configure_path)
    return ret


def _materialize_configure_payload(request, argv):
    configure_payload = request.get("configure")
    if not configure_payload:
        return (argv, None)

    configure_content = base64.b64decode(
        configure_payload.get("content", "").encode("ascii"))
    suffix = configure_payload.get("suffix", ".yaml")
    fd, configure_path = tempfile.mkstemp(prefix="generate-for-pb-",
                                         suffix=suffix)
    try:
        os.write(fd, configure_content)
    finally:
        os.close(fd)
    return (_replace_configure_arg(argv, configure_path), configure_path)


def run_generation_request(request, main_func):
    action = request.get("action")
    if action == "ping":
        return {
            "returncode": 0,
            "stdout": "",
            "stderr": "",
            "pid": os.getpid(),
            "process_path": _get_process_image_path(os.getpid()),
        }
    if action == "shutdown":
        return {
            "returncode": 0,
            "stdout": "[INFO]: generator server shutdown requested.\n",
            "stderr": "",
        }
    if action != "generate":
        return {
            "returncode": 1,
            "stdout": "",
            "stderr": "[ERROR]: unknown generator server action: {0}\n".
            format(action),
        }

    request_argv = list(request.get("argv") or [])
    display_argv = request.get("display_argv")
    cwd = request.get("cwd")
    old_cwd = os.getcwd()
    tmp_configure_path = None
    stdout_buffer = io.StringIO()
    stderr_buffer = io.StringIO()
    return_code = 1
    try:
        if cwd:
            os.chdir(cwd)
        request_argv, tmp_configure_path = _materialize_configure_payload(
            request, request_argv)
        with contextlib.redirect_stdout(stdout_buffer), contextlib.redirect_stderr(
                stderr_buffer):
            return_code = main_func(argv=request_argv,
                                    display_argv=display_argv,
                                    allow_ipc=False)
    except SystemExit as e:
        return_code = normalize_return_code(e.code)
    except BaseException:
        return_code = 1
        stderr_buffer.write(traceback.format_exc())
    finally:
        if tmp_configure_path:
            try:
                os.remove(tmp_configure_path)
            except OSError:
                pass
        os.chdir(old_cwd)

    return {
        "returncode": normalize_return_code(return_code),
        "stdout": stdout_buffer.getvalue(),
        "stderr": stderr_buffer.getvalue(),
    }


class _GeneratorServer(socketserver.TCPServer):
    allow_reuse_address = True
    request_queue_size = 128

    def __init__(self, server_address, request_handler_class,
                 request_callback, idle_timeout):
        socketserver.TCPServer.__init__(self, server_address,
                                        request_handler_class)
        self.request_callback = request_callback
        self.idle_timeout = idle_timeout
        self._activity_lock = threading.Lock()
        self._active_requests = 0
        self._last_activity_time = time.monotonic()
        self._stop_monitor_event = threading.Event()

    def mark_request_begin(self):
        with self._activity_lock:
            self._active_requests += 1

    def mark_request_end(self):
        with self._activity_lock:
            if self._active_requests > 0:
                self._active_requests -= 1
            self._last_activity_time = time.monotonic()

    def is_idle_timeout_expired(self):
        if self.idle_timeout is None or self.idle_timeout <= 0:
            return False
        with self._activity_lock:
            if self._active_requests > 0:
                return False
            return time.monotonic() - self._last_activity_time >= self.idle_timeout

    def stop_idle_monitor(self):
        self._stop_monitor_event.set()


class _GeneratorServerHandler(socketserver.BaseRequestHandler):

    def handle(self):
        shutdown_requested = False
        self.server.mark_request_begin()
        try:
            request = _recv_json(self.request)
            if request.get("magic") != GENERATOR_IPC_MAGIC:
                raise ValueError("invalid generator IPC magic")
            if request.get("version") != GENERATOR_IPC_VERSION:
                raise ValueError("unsupported generator IPC version: {0}".
                                 format(request.get("version")))
            shutdown_requested = request.get("action") == "shutdown"
            response = self.server.request_callback(request)
        except BaseException:
            response = {
                "returncode": 1,
                "stdout": "",
                "stderr": traceback.format_exc(),
            }

        try:
            _send_json(self.request, response)
        finally:
            self.server.mark_request_end()
            if shutdown_requested:
                threading.Thread(target=self.server.shutdown).start()


def _run_idle_monitor(server):
    while not server._stop_monitor_event.wait(1.0):
        if not server.is_idle_timeout_expired():
            continue
        sys.stdout.write(
            "[INFO]: generator server idle for {0} seconds, shutting down.\n".
            format(server.idle_timeout))
        sys.stdout.flush()
        server.shutdown()
        return


def run_generator_server(address, idle_timeout, request_callback, pid_file=None):
    idle_timeout = normalize_timeout(idle_timeout,
                                     GENERATOR_IPC_DEFAULT_IDLE_TIMEOUT)
    pid_file = normalize_pid_file_path(pid_file, os.getcwd())
    server_address = parse_server_address(address)
    server = _GeneratorServer(server_address, _GeneratorServerHandler,
                              request_callback, idle_timeout)
    host, port = server.server_address
    try:
        _write_pid_file(pid_file, os.getpid())
    except BaseException as e:
        server.server_close()
        sys.stderr.write(
            "[ERROR]: write generator server pid file {0} failed: {1}\n".
            format(pid_file, e))
        return 1
    sys.stdout.write(
        "[INFO]: generator server listening on {0}:{1}\n".format(host, port))
    sys.stdout.flush()
    idle_monitor = threading.Thread(target=_run_idle_monitor, args=[server])
    idle_monitor.daemon = True
    idle_monitor.start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        sys.stdout.write("[INFO]: generator server stopped by user.\n")
    finally:
        server.stop_idle_monitor()
        server.server_close()
        _remove_pid_file_if_match(pid_file, os.getpid())
    return 0


def _connect_and_request(address, connect_timeout, read_timeout, request):
    sock = socket.create_connection(parse_server_address(address),
                                    timeout=connect_timeout)
    try:
        sock.settimeout(read_timeout)
        _send_json(sock, request)
        return _recv_json(sock)
    finally:
        sock.close()


def _start_generator_server(server_program, address, idle_timeout, cwd,
                            bootstrap_args, pid_file):
    if not server_program:
        raise RuntimeError("can not auto start generator server without script path")

    server_args = [
        sys.executable,
        server_program,
        "--server-mode",
        "--server-address",
        address,
        "--server-idle-timeout",
        str(idle_timeout),
    ]
    if pid_file:
        server_args.extend(["--server-pid-file", pid_file])
    server_args.extend(bootstrap_args)
    creationflags = 0
    start_new_session = False
    if os.name == "nt":
        creationflags = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
        creationflags |= getattr(subprocess, "DETACHED_PROCESS", 0)
    else:
        start_new_session = True
    return subprocess.Popen(
        server_args,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        cwd=cwd,
        close_fds=True,
        creationflags=creationflags,
        start_new_session=start_new_session,
    )


def _wait_generator_server_ready(address, timeout, pid_file=None,
                                 expected_process_path=None):
    deadline = time.monotonic() + max(timeout, 0.1)
    last_error = None
    while time.monotonic() < deadline:
        try:
            if pid_file:
                pid_file_ready, pid_file_error = _check_pid_file_process(
                    pid_file, expected_process_path)
                if not pid_file_ready:
                    last_error = RuntimeError(pid_file_error)
                    time.sleep(0.05)
                    continue
            _ping_generator_server(address, 0.2, 0.2, pid_file,
                                   expected_process_path)
            return True
        except BaseException as e:
            last_error = e
            time.sleep(0.05)
    if last_error:
        raise last_error
    return False


def run_generator_client(address,
                         timeout,
                         argv,
                         cwd,
                         display_argv0,
                         shutdown,
                         auto_start=True,
                         idle_timeout=GENERATOR_IPC_DEFAULT_IDLE_TIMEOUT,
                         server_program=None,
                         pid_file=None):
    timeout = normalize_timeout(timeout, GENERATOR_IPC_DEFAULT_TIMEOUT)
    idle_timeout = normalize_timeout(idle_timeout,
                                     GENERATOR_IPC_DEFAULT_IDLE_TIMEOUT)
    pid_file = normalize_pid_file_path(pid_file, cwd)
    expected_process_path = _get_process_image_path(os.getpid())
    request_argv = [] if shutdown else list(argv)
    request = {
        "magic": GENERATOR_IPC_MAGIC,
        "version": GENERATOR_IPC_VERSION,
        "action": "shutdown" if shutdown else "generate",
        "argv": request_argv,
        "cwd": cwd,
        "display_argv": [display_argv0] + request_argv,
    }
    if not shutdown:
        configure_payload = _collect_configure_payload(request_argv, cwd)
        if configure_payload is not None:
            request["configure"] = configure_payload

    connect_timeout = min(max(timeout, 0.1), 1.0)
    try:
        if pid_file:
            pid_file_ready, pid_file_error = _check_pid_file_process(
                pid_file, expected_process_path)
            if not pid_file_ready:
                raise RuntimeError(pid_file_error)
            _ping_generator_server(address, connect_timeout, timeout, pid_file,
                                   expected_process_path)
        response = _connect_and_request(address, connect_timeout, timeout,
                                        request)
    except BaseException as e:
        if shutdown or not auto_start:
            sys.stderr.write(
                "[ERROR]: generator client request failed: {0}\n".format(e))
            return 1
        try:
            bootstrap_args = collect_generator_server_bootstrap_args(argv)
            server_process = _start_generator_server(
                server_program, address, idle_timeout, cwd, bootstrap_args,
                pid_file)
            _write_pid_file(pid_file, server_process.pid)
            _wait_generator_server_ready(address, min(max(timeout, 1.0), 10.0),
                                         pid_file, expected_process_path)
            response = _connect_and_request(address, connect_timeout, timeout,
                                            request)
        except BaseException as start_error:
            sys.stderr.write(
                "[ERROR]: generator client request failed: {0}\n".format(e))
            sys.stderr.write(
                "[ERROR]: auto start generator server failed: {0}\n".format(
                    start_error))
            return 1

    stdout_content = response.get("stdout")
    stderr_content = response.get("stderr")
    if stdout_content:
        sys.stdout.write(stdout_content)
    if stderr_content:
        sys.stderr.write(stderr_content)
    return normalize_return_code(response.get("returncode", 1))
