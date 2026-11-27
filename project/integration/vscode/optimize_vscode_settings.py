#!/usr/bin/env python3
# Copyright 2026 atframework
"""Idempotently optimize the workspace ``.vscode/settings.json`` for atsf4g-co.

The script is normally driven by ``OptimizeVsCodeSettings.cmake`` (which discovers the
compiler, build directory and helper tools), but it can also be run standalone.

Applied rules (existing user values are preserved unless a repair is required):

* ``cmake.configureEnvironment`` gains ``CPRINTF_MODE=none`` (configure only).
* ``cmake.environment`` gains ``VSLANG=1033`` (applies to configure **and** build).
  ``VSLANG`` must reach the build so cl.exe emits the English ``/showIncludes`` prefix
  that CMake+Ninja match for header/PCH dependency tracking under a non-UTF-8 system
  codepage; putting it only in ``cmake.configureEnvironment`` leaves the build broken.
* Each key is added only when missing (present keys are never overwritten); a stale
  ``VSLANG`` left in ``cmake.configureEnvironment`` by an older version of this script is
  migrated to ``cmake.environment``.
* ``cpplint.cpplintPath`` is filled or repaired only when unset or pointing at a
  missing executable; a still-valid user path is kept.
* ``cmakeFormat.exePath`` is filled or repaired the same way when the installed
  ``cheshirekow.cmake-format`` extension contributes that setting. The executable is
  resolved only from the Python interpreter running this script, so CMake's selected
  ``Python3_EXECUTABLE`` determines the environment; unrelated ``PATH`` installations
  are not used as fallbacks.
* ``protobuf.protoc.path`` (for ``drblury.protobuf-vsc`` / Protobuf VSC, and ``protoc.path``
  for ``zxh404.vscode-proto3`` when used) is filled or repaired the same way using the
  project's resolved third-party ``protoc`` executable (since VS Code extension hosts do not
  inherit ``terminal.integrated.env.*`` or CMake runtime environments). An absolute path is
  written because parts of ``drblury.protobuf-vsc`` spawn ``protobuf.protoc.path`` directly
  without expanding ``${workspaceFolder}``.
* ``C_Cpp.clang_format_path`` is filled or repaired the same way, but only when
  ``C_Cpp.intelliSenseEngine`` is not ``disabled``.
* When clangd is in use, ``clangd.arguments`` is normalized:
    - ``--compile-commands-dir=<build dir>`` is appended only when absent.
    - ``--query-driver`` is repaired to the real MSVC driver (MSVC compilers only).
    - ``--background-index`` and ``--clang-tidy`` are ensured.
    - ``--header-insertion`` is forced to ``never`` (the only value clangd accepts;
      note that ``Never`` / ``iwyu`` casing variants are rejected by clangd).
* File-watcher / search exclusions are ensured to keep the editor responsive on workspaces
  with a large build tree. ``files.watcherExclude`` and ``search.exclude`` gain the configured
  build directory (``--build-dir``, sourced by the CMake driver from
  ``CMAKE_CURRENT_BINARY_DIR``), the generic ``**/build/**`` output directory, and the
  append-only ``**/.git/objects/**`` / ``**/.git/subtree-cache/**``. ``files.exclude`` is
  intentionally NOT touched: it hides entries from the Explorer, which makes browsing the
  workspace inconvenient. Existing globs under the touched keys are always kept.
* ``cpplint.excludes``, ``pylint.ignorePatterns`` and ``python.analysis.exclude`` gain the same
  build directory plus the generic ``build`` output dir so static analysis skips generated code.
  These keys are arrays and are merged the same way (existing entries kept). Only the actual
  build dir name and the exact ``build`` are added, never a broad ``build_*`` wildcard, so a
  source directory such as ``.../build_setting`` stays analyzed and searchable.
* When third-party runtime directories (``--extra-bin-dir`` / ``--extra-lib-dir``) are supplied,
  they are merged idempotently into the runtime scopes for the active host platform without
  touching ``cmake.environment`` (avoiding Windows MSVC ``vcvarsall`` PATH clobbering and
  configure-time host tool library conflicts):
    - ``terminal.integrated.env.windows`` / ``.linux`` / ``.osx`` (OS-scoped integrated terminal
      and VS Code tasks).
    - ``cmake.testEnvironment`` (CTest runs).
    - ``cmake.debugConfig`` (CMake Tools debug/launch: ``environment`` array for ``cppvsdbg`` /
      ``cppdbg``, or ``env`` dict when ``type`` is ``lldb``).
    - Per-platform variable mapping:
        * Windows: ``bin`` and ``lib``/``lib64`` directories are added to ``PATH`` (``Path`` when
          creating a new entry in ``cmake.debugConfig.environment`` for ``cppvsdbg``).
        * Linux: ``bin`` -> ``PATH``, ``lib``/``lib64`` -> ``LD_LIBRARY_PATH``.
        * macOS: ``bin`` -> ``PATH``, ``lib``/``lib64`` -> ``DYLD_FALLBACK_LIBRARY_PATH``.

JSONC comments are not preserved on rewrite; a ``.bak`` copy is written before any change.
"""

import argparse
import copy
import importlib.util
import json
import ntpath
import os
import posixpath
import re
import shutil
import sys
import sysconfig

# Env vars required during CMake configure only.
DESIRED_CONFIGURE_ENVIRONMENT = (("CPRINTF_MODE", "none"),)

# Env vars required for configure AND build, so they live in the shared ``cmake.environment``
# block. VSLANG=1033 forces cl.exe to emit the English ``/showIncludes`` prefix that
# CMake+Ninja match for header/PCH dependency tracking under a non-UTF-8 system codepage; it
# must reach the build step, not only configure.
DESIRED_ENVIRONMENT = (("VSLANG", "1033"),)

# Keys an older version of this script injected into ``cmake.configureEnvironment`` but that
# now belong in ``cmake.environment``; migrated away on every run for correctness.
MIGRATE_TO_ENVIRONMENT = ("VSLANG",)

# Static exclusion glob patterns applied to the file watcher and the search scope (but NOT to
# ``files.exclude``, which hides entries from the Explorer). They are the main cure for VSCode
# watcher/index latency on workspaces that carry multi-GB build output trees: the build step
# writes thousands of .obj/.pdb/.ilk/.log files, and every change notification has to flow
# through the watcher queue before an editor save or directory create is acknowledged.
#
# ``build`` is the default output directory across most toolchains (and every vendored
# subproject that owns an independent tree, e.g. ``atframe_utils/build``); the any-depth form
# adapts to all of them while matching only the exact ``build`` name, so a source directory such
# as ``.../build_setting`` is never hidden. The actual configured build tree (``--build-dir``,
# which may be named ``build_jobs_*`` etc.) is added separately from it.
# ``.git/objects`` and ``.git/subtree-cache`` are very large, append-only, and never edited.
GENERIC_BUILD_EXCLUDE_GLOB = "**/build/**"

STATIC_EXCLUDE_GLOBS = (
    GENERIC_BUILD_EXCLUDE_GLOB,
    "**/.git/objects/**",
    "**/.git/subtree-cache/**",
)

CMAKE_FORMAT_EXTENSION_ID = "cheshirekow.cmake-format"
CMAKE_FORMAT_PATH_SETTING = "cmakeFormat.exePath"
PROTOBUF_VSC_EXTENSION_ID = "drblury.protobuf-vsc"
PROTOBUF_VSC_PROTOC_PATH_SETTING = "protobuf.protoc.path"
VSCODE_PROTO3_EXTENSION_ID = "zxh404.vscode-proto3"
VSCODE_PROTO3_PROTOC_SECTION = "protoc"
VSCODE_EXTENSION_ROOTS = (
    ".vscode/extensions",
    ".vscode-insiders/extensions",
    ".vscode-server/extensions",
    ".vscode-server-insiders/extensions",
)

PLATFORM_WINDOWS = "windows"
PLATFORM_LINUX = "linux"
PLATFORM_OSX = "osx"
SUPPORTED_PLATFORMS = (PLATFORM_WINDOWS, PLATFORM_LINUX, PLATFORM_OSX)
TERMINAL_ENV_SETTING_BY_PLATFORM = {
    PLATFORM_WINDOWS: "terminal.integrated.env.windows",
    PLATFORM_LINUX: "terminal.integrated.env.linux",
    PLATFORM_OSX: "terminal.integrated.env.osx",
}


def strip_jsonc_comments(text):
    """Remove ``//`` and ``/* */`` comments while respecting string literals."""
    out = []
    i, n = 0, len(text)
    in_string = False
    while i < n:
        ch = text[i]
        if in_string:
            out.append(ch)
            if ch == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if ch == '"':
                in_string = False
            i += 1
            continue
        if ch == '"':
            in_string = True
            out.append(ch)
            i += 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            i += 2
            while i < n and text[i] not in "\r\n":
                i += 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def load_settings(path):
    """Return ``(data, had_comments)`` for a JSONC settings file (``{}`` when absent)."""
    if not os.path.isfile(path):
        return {}, False
    with open(path, "r", encoding="utf-8-sig") as handle:
        raw = handle.read()
    stripped = strip_jsonc_comments(raw)
    had_comments = stripped != raw
    try:
        data = json.loads(stripped)
    except json.JSONDecodeError:
        # Tolerate trailing commas, which are legal in VS Code JSONC.
        data = json.loads(re.sub(r",(\s*[}\]])", r"\1", stripped))
    if not isinstance(data, dict):
        raise ValueError("the root of settings.json must be a JSON object")
    return data, had_comments


def normalize_path(path):
    text = str(path).replace("\\", "/")
    # On Windows, shutil.which/PATHEXT may yield an upper-case extension (cpplint.EXE);
    # lower-case it to match the repository convention and avoid needless churn.
    if os.name == "nt":
        root, ext = os.path.splitext(text)
        if ext:
            text = root + ext.lower()
    return text


def is_usable_program(path):
    """Whether ``path`` currently resolves to an existing executable."""
    if path is None:
        return False
    text = str(path).strip()
    if not text:
        return False
    # VS Code variables such as ${workspaceFolder} cannot be validated here; trust the user.
    if "${" in text:
        return True
    expanded = os.path.expanduser(os.path.expandvars(text))
    if os.path.dirname(expanded):
        return os.path.isfile(expanded)
    return shutil.which(expanded) is not None


def resolve_tool(hint, names):
    """Return a usable executable path, preferring ``hint`` then a PATH lookup."""
    if hint and is_usable_program(hint):
        return normalize_path(hint)
    for name in names:
        found = shutil.which(name)
        if found:
            return normalize_path(found)
    return None


def is_vscode_extension_setting_available(
    target_extension_id, setting_key, extension_roots=None
):
    """Whether an installed VS Code extension contributes ``setting_key``."""
    target_id = target_extension_id.lower()
    roots = (
        extension_roots
        if extension_roots is not None
        else [os.path.expanduser("~/" + rel) for rel in VSCODE_EXTENSION_ROOTS]
    )
    for root in roots:
        try:
            extension_dirs = os.listdir(root)
        except OSError:
            continue

        obsolete = {}
        try:
            with open(os.path.join(root, ".obsolete"), encoding="utf-8-sig") as handle:
                loaded_obsolete = json.load(handle)
            if isinstance(loaded_obsolete, dict):
                obsolete = loaded_obsolete
        except (OSError, ValueError):
            pass

        for extension_dir in extension_dirs:
            normalized_dir = extension_dir.lower()
            if not (
                normalized_dir == target_id
                or normalized_dir.startswith(target_id + "-")
            ):
                continue
            if obsolete.get(extension_dir) is True:
                continue

            try:
                with open(
                    os.path.join(root, extension_dir, "package.json"),
                    encoding="utf-8-sig",
                ) as handle:
                    manifest = json.load(handle)
            except (OSError, ValueError):
                continue
            if not isinstance(manifest, dict):
                continue
            extension_id = "%s.%s" % (
                manifest.get("publisher", ""),
                manifest.get("name", ""),
            )
            if extension_id.lower() != target_id:
                continue

            contributes = manifest.get("contributes")
            if not isinstance(contributes, dict):
                continue
            configurations = contributes.get("configuration", [])
            if isinstance(configurations, dict):
                configurations = [configurations]
            if not isinstance(configurations, list):
                continue
            for configuration in configurations:
                if not isinstance(configuration, dict):
                    continue
                properties = configuration.get("properties")
                if isinstance(properties, dict) and setting_key in properties:
                    return True
    return False


def is_cmake_format_extension_available():
    """Whether an installed VS Code extension contributes ``cmakeFormat.exePath``."""
    return is_vscode_extension_setting_available(
        CMAKE_FORMAT_EXTENSION_ID, CMAKE_FORMAT_PATH_SETTING
    )


def is_protobuf_vsc_extension_available():
    """Whether ``drblury.protobuf-vsc`` (Protobuf VSC) contributes ``protobuf.protoc.path``."""
    return is_vscode_extension_setting_available(
        PROTOBUF_VSC_EXTENSION_ID, PROTOBUF_VSC_PROTOC_PATH_SETTING
    )


def is_vscode_proto3_extension_available():
    """Whether ``zxh404.vscode-proto3`` contributes ``protoc`` settings."""
    return is_vscode_extension_setting_available(
        VSCODE_PROTO3_EXTENSION_ID, VSCODE_PROTO3_PROTOC_SECTION
    )


def resolve_protoc(hint, extra_bin_dirs, platform):
    """Find ``protoc``, preferring ``hint`` then ``extra_bin_dirs`` before system ``PATH``."""
    if hint and is_usable_program(hint):
        return normalize_path(hint)
    executable_names = (
        ("protoc.exe", "protoc") if platform == PLATFORM_WINDOWS else ("protoc",)
    )
    for bin_dir in extra_bin_dirs:
        if not bin_dir or not str(bin_dir).strip():
            continue
        for name in executable_names:
            candidate = os.path.join(str(bin_dir), name)
            if os.path.isfile(candidate):
                return normalize_path(candidate)
    return resolve_tool(None, executable_names)


def apply_protobuf_protoc_path(data, opts, changes):
    """Fill or repair the protoc executable path for installed Protobuf VS Code extensions."""
    has_protobuf_vsc = is_protobuf_vsc_extension_available()
    has_vscode_proto3 = is_vscode_proto3_extension_available()
    configure_protobuf_vsc = has_protobuf_vsc or (
        PROTOBUF_VSC_PROTOC_PATH_SETTING in data
    )
    existing_proto3_section = data.get(VSCODE_PROTO3_PROTOC_SECTION)
    configure_vscode_proto3 = has_vscode_proto3 and (
        isinstance(existing_proto3_section, dict) or not configure_protobuf_vsc
    )
    if not configure_protobuf_vsc and not configure_vscode_proto3:
        return

    platform = detect_host_platform(opts.platform)
    resolved = resolve_protoc(opts.protoc, opts.extra_bin_dirs, platform)

    if configure_protobuf_vsc:
        apply_tool_path(
            data, PROTOBUF_VSC_PROTOC_PATH_SETTING, resolved, (), changes
        )

    if configure_vscode_proto3:
        section = data.get(VSCODE_PROTO3_PROTOC_SECTION)
        if section is not None and not isinstance(section, dict):
            print(
                "[optimize-vscode] skip %s: existing value is not an object"
                % VSCODE_PROTO3_PROTOC_SECTION,
                file=sys.stderr,
            )
            return
        if section is None:
            section = {}
        current = section.get("path")
        if is_usable_program(current):
            return
        if resolved is None:
            print(
                "[optimize-vscode] protoc not found; leaving %s.path unchanged"
                % VSCODE_PROTO3_PROTOC_SECTION,
                file=sys.stderr,
            )
            return
        if current == resolved:
            return
        section["path"] = resolved
        data[VSCODE_PROTO3_PROTOC_SECTION] = section
        suffix = " (previous path was invalid)" if current else ""
        changes.append(
            "%s.path: set %s%s" % (VSCODE_PROTO3_PROTOC_SECTION, resolved, suffix)
        )


def resolve_python_cmake_format():
    """Find cmake-format installed for the interpreter running this script."""
    if importlib.util.find_spec("cmakelang") is None:
        return None

    script_dirs = [sysconfig.get_path("scripts")]
    for scheme in sorted(sysconfig.get_scheme_names()):
        if scheme.endswith("_user"):
            script_dir = sysconfig.get_path("scripts", scheme=scheme)
            if script_dir not in script_dirs:
                script_dirs.append(script_dir)

    executable_names = (
        ("cmake-format.exe", "cmake-format")
        if os.name == "nt"
        else ("cmake-format",)
    )
    for script_dir in script_dirs:
        if not script_dir:
            continue
        for executable_name in executable_names:
            candidate = os.path.join(script_dir, executable_name)
            if is_usable_program(candidate):
                return normalize_path(candidate)
    return None


def compute_compile_commands_dir(build_dir, workspace_dir):
    """Prefer a ``${workspaceFolder}``-relative value, else an absolute path."""
    build_abs = os.path.abspath(build_dir)
    workspace_abs = os.path.abspath(workspace_dir)
    try:
        rel = os.path.relpath(build_abs, workspace_abs)
    except ValueError:
        rel = None
    if rel == ".":
        return "${workspaceFolder}"
    if rel and not rel.startswith("..") and not os.path.isabs(rel):
        return "${workspaceFolder}/" + normalize_path(rel)
    return normalize_path(build_abs)


def compute_query_driver(compiler_path):
    """Return the concrete compiler path clangd may query for system includes.

    clangd matches the driver recorded in compile_commands.json against this glob
    whitelist; using the exact resolved compiler is the deterministic correct value,
    and (unlike a re-globbed path) it actually reflects the currently configured toolchain.
    """
    return normalize_path(compiler_path)


def find_arg_index(args, flag):
    """Index of ``flag`` (bare or ``flag=value``) in ``args``; -1 when absent.

    Matching is exact on the flag name so that sibling flags sharing a prefix
    (``--header-insertion`` vs ``--header-insertion-decorators``) are not confused.
    """
    prefix = flag + "="
    for index, value in enumerate(args):
        if value == flag or value.startswith(prefix):
            return index
    return -1


def compute_build_dir_glob(build_dir, workspace_dir):
    """Return the glob matching the build directory node itself (no trailing ``/**``).

    Expressed relative to the workspace when possible (``**/<basename>``) so it stays valid even
    if the absolute path moves; an out-of-workspace tree falls back to its absolute path. Callers
    append the tool-specific suffix (``/**`` for watcher/search/cpplint, ``/**/*.py`` for pylint,
    nothing for pyright's directory exclude). ``None`` when no build directory was supplied.
    """
    if not build_dir:
        return None
    build_abs = os.path.abspath(build_dir)
    workspace_abs = os.path.abspath(workspace_dir)
    try:
        rel = os.path.relpath(build_abs, workspace_abs)
    except ValueError:
        rel = None
    if rel == ".":
        # Never exclude the workspace root itself on an in-source build.
        return None
    if rel and not rel.startswith("..") and not os.path.isabs(rel):
        return "**/%s" % normalize_path(rel)
    return normalize_path(build_abs)


def compute_build_exclude_glob(build_dir, workspace_dir):
    """Return the ``files.watcherExclude``-style glob that matches the whole build tree.

    The build directory (``--build-dir``, sourced by the CMake driver from
    ``CMAKE_CURRENT_BINARY_DIR``) is the largest source of file-change churn. A workspace-relative
    node (``**/<basename>``) gains a ``/**`` suffix so the whole subtree is skipped; an absolute
    out-of-workspace path is matched as-is. ``None`` is returned when no build dir was supplied.
    """
    dir_glob = compute_build_dir_glob(build_dir, workspace_dir)
    if dir_glob is None:
        return None
    return dir_glob + "/**" if dir_glob.startswith("**/") else dir_glob


def apply_glob_block(data, key, globs, changes):
    """Ensure each glob in ``globs`` is present under settings key ``key`` (idempotent).

    Existing entries are never overwritten; the block is created when missing and left as-is
    when it already holds a non-object value (a warning is printed instead). Mirrors the
    defensive behaviour of :func:`apply_environment_block`.
    """
    block = data.get(key)
    if block is not None and not isinstance(block, dict):
        print(
            "[optimize-vscode] skip %s: existing value is not an object" % key,
            file=sys.stderr,
        )
        return
    if block is None:
        block = {}
    for glob in globs:
        if glob is None or glob in block:
            continue
        block[glob] = True
        changes.append("%s: add %s" % (key, glob))
    data[key] = block


def apply_list_block(data, key, values, changes):
    """Ensure each entry in ``values`` is present in the array at settings key ``key``.

    Existing entries are never removed or reordered; the array is created when missing and left
    as-is (with a warning) when it already holds a non-array value. Mirrors :func:`apply_glob_block`
    for keys whose VS Code schema is a list (``cpplint.excludes``, ``pylint.ignorePatterns``,
    ``python.analysis.exclude``) rather than an object.
    """
    block = data.get(key)
    if block is not None and not isinstance(block, list):
        print(
            "[optimize-vscode] skip %s: existing value is not an array" % key,
            file=sys.stderr,
        )
        return
    if block is None:
        block = []
    for value in values:
        if value is None or value in block:
            continue
        block.append(value)
        changes.append("%s: add %s" % (key, value))
    data[key] = block


def apply_environment_block(data, key, desired, changes):
    """Ensure each (name, value) in ``desired`` is present under settings key ``key``.

    Existing keys are never overwritten; the block is created when missing and left as-is
    when it already holds a non-object value (a warning is printed instead).
    """
    env = data.get(key)
    if env is not None and not isinstance(env, dict):
        print(
            "[optimize-vscode] skip %s: existing value is not an object" % key,
            file=sys.stderr,
        )
        return
    if env is None:
        env = {}
    for name, value in desired:
        if name not in env:
            env[name] = value
            changes.append("%s: add %s=%s" % (key, name, value))
    data[key] = env


def migrate_environment_keys(data, src_key, dst_key, names, changes):
    """Move ``names`` from settings block ``src_key`` to ``dst_key`` (idempotent).

    Used to clean up keys a previous version of this script placed in the wrong block.
    Only the listed names are moved; everything else in ``src_key`` is left untouched.
    """
    src = data.get(src_key)
    if not isinstance(src, dict):
        return
    dst = data.get(dst_key)
    if dst is None:
        dst = {}
    elif not isinstance(dst, dict):
        return
    touched = False
    for name in names:
        if name in src:
            value = src.pop(name)
            touched = True
            if name not in dst:
                dst[name] = value
                changes.append(
                    "%s: add %s=%s (migrated from %s)" % (dst_key, name, value, src_key)
                )
            else:
                changes.append(
                    "%s: drop redundant %s (already in %s)" % (src_key, name, dst_key)
                )
    if touched:
        data[dst_key] = dst
        data[src_key] = src


def apply_tool_path(data, key, hint, names, changes):
    current = data.get(key)
    if is_usable_program(current):
        return
    resolved = resolve_tool(hint, names)
    if resolved is None:
        tool_name = names[0] if names else key
        print(
            "[optimize-vscode] %s not found; leaving %s unchanged"
            % (tool_name, key),
            file=sys.stderr,
        )
        return
    if current == resolved:
        return
    data[key] = resolved
    suffix = " (previous path was invalid)" if current else ""
    changes.append("%s: set %s%s" % (key, resolved, suffix))


def is_using_clangd(data, intellisense_disabled):
    enable = data.get("clangd.enable")
    if enable is True:
        return True
    if "clangd.arguments" in data:
        return True
    return intellisense_disabled and enable is not False


def apply_clangd_arguments(data, opts, changes):
    args = data.get("clangd.arguments")
    if args is None:
        args = []
    elif not isinstance(args, list):
        print(
            "[optimize-vscode] skip clangd.arguments: existing value is not an array",
            file=sys.stderr,
        )
        return

    if opts.build_dir and find_arg_index(args, "--compile-commands-dir") < 0:
        value = compute_compile_commands_dir(opts.build_dir, opts.workspace_dir)
        args.append("--compile-commands-dir=%s" % value)
        changes.append("clangd.arguments: add --compile-commands-dir=%s" % value)

    if (opts.compiler_id or "").upper() == "MSVC" and opts.compiler_path:
        desired = "--query-driver=%s" % compute_query_driver(opts.compiler_path)
        index = find_arg_index(args, "--query-driver")
        if index < 0:
            args.append(desired)
            changes.append("clangd.arguments: add %s" % desired)
        elif args[index] != desired:
            changes.append("clangd.arguments: repair %s -> %s" % (args[index], desired))
            args[index] = desired

    for flag in ("--background-index", "--clang-tidy"):
        if find_arg_index(args, flag) < 0:
            args.append(flag)
            changes.append("clangd.arguments: add %s" % flag)

    desired_header = "--header-insertion=never"
    index = find_arg_index(args, "--header-insertion")
    if index < 0:
        args.append(desired_header)
        changes.append("clangd.arguments: add %s" % desired_header)
    elif args[index] != desired_header:
        changes.append(
            "clangd.arguments: override %s -> %s" % (args[index], desired_header)
        )
        args[index] = desired_header

    data["clangd.arguments"] = args


def detect_host_platform(explicit=None):
    """Return ``windows``, ``linux``, or ``osx`` for the active host platform."""
    if explicit:
        return explicit
    if os.name == "nt" or sys.platform.startswith(("win", "msys", "cygwin")):
        return PLATFORM_WINDOWS
    if sys.platform == "darwin":
        return PLATFORM_OSX
    return PLATFORM_LINUX


def get_path_module(platform):
    return ntpath if platform == PLATFORM_WINDOWS else posixpath


def normalize_platform_path(path, platform, base_dir=None):
    """Normalize ``path`` using ``platform`` path semantics."""
    pathmod = get_path_module(platform)
    text = str(path).strip()
    if platform == PLATFORM_WINDOWS:
        text = text.replace("/", "\\")
        if base_dir and not pathmod.isabs(text):
            text = pathmod.join(str(base_dir).strip().replace("/", "\\"), text)
    else:
        text = text.replace("\\", "/")
        if base_dir and not pathmod.isabs(text):
            text = pathmod.join(str(base_dir).strip().replace("\\", "/"), text)
    return pathmod.normpath(text)


def format_env_dir(path, workspace_dir, platform):
    """Format a runtime env directory, preferring ``${workspaceFolder}``-relative paths."""
    pathmod = get_path_module(platform)
    workspace_norm = normalize_platform_path(workspace_dir, platform)
    expanded = expand_workspace_tokens(path, workspace_norm, platform)
    dir_norm = normalize_platform_path(expanded, platform, base_dir=workspace_norm)
    try:
        rel = pathmod.relpath(dir_norm, workspace_norm)
    except ValueError:
        rel = None
    if rel == ".":
        return "${workspaceFolder}"
    if (
        rel
        and rel != ".."
        and not rel.startswith(".." + pathmod.sep)
        and not pathmod.isabs(rel)
    ):
        if platform == PLATFORM_WINDOWS:
            return "${workspaceFolder}\\" + rel.replace("/", "\\")
        return "${workspaceFolder}/" + rel.replace("\\", "/")
    return dir_norm


def expand_workspace_tokens(entry, workspace_dir, platform):
    """Expand ``${workspaceFolder}`` / ``${workspaceRoot}`` and separator tokens for comparison."""
    pathmod = get_path_module(platform)
    workspace_norm = normalize_platform_path(workspace_dir, platform)
    token = str(entry).strip()
    if len(token) >= 2 and token[0] == '"' and token[-1] == '"':
        token = token[1:-1].strip()
    token = token.replace("${pathSeparator}", pathmod.sep).replace("${/}", pathmod.sep)
    for var_token in ("${workspaceFolder}", "${workspaceRoot}"):
        if token == var_token:
            return workspace_norm
        for sep_char in ("/", "\\"):
            prefix = var_token + sep_char
            if token.startswith(prefix):
                return pathmod.join(workspace_norm, token[len(prefix) :])
    return token


def canonical_env_entry_key(entry, workspace_dir, platform):
    """Return a normalized comparison key so equivalent path spellings deduplicate cleanly."""
    expanded = expand_workspace_tokens(entry, workspace_dir, platform)
    if not expanded:
        return ""
    if "${" in expanded or "$" in expanded or "%" in expanded:
        return expanded.lower() if platform == PLATFORM_WINDOWS else expanded
    normalized = normalize_platform_path(expanded, platform)
    if platform == detect_host_platform() and os.path.exists(normalized):
        normalized = normalize_platform_path(os.path.realpath(normalized), platform)
    if platform == PLATFORM_WINDOWS:
        # Strip trailing backslash except on drive roots such as C:\.
        if len(normalized) > 3 and normalized.endswith("\\"):
            normalized = normalized.rstrip("\\")
        return normalized.lower()
    if len(normalized) > 1 and normalized.endswith("/"):
        normalized = normalized.rstrip("/")
    return normalized


def deduplicate_env_dirs(dirs, workspace_dir, platform):
    """Format and deduplicate directories while preserving first-seen order."""
    result = []
    seen = set()
    for raw_dir in dirs:
        if raw_dir is None or not str(raw_dir).strip():
            continue
        formatted = format_env_dir(raw_dir, workspace_dir, platform)
        key = canonical_env_entry_key(formatted, workspace_dir, platform)
        if not key or key in seen:
            continue
        seen.add(key)
        result.append(formatted)
    return result


def split_env_path_entries(value, sep):
    """Split a PATH-like string on ``sep`` without splitting inside ``${...}`` or quotes."""
    entries = []
    current = []
    brace_depth = 0
    in_quotes = False
    i = 0
    n = len(value)
    while i < n:
        ch = value[i]
        if ch == '"':
            in_quotes = not in_quotes
            current.append(ch)
            i += 1
            continue
        if not in_quotes:
            if ch == "$" and i + 1 < n and value[i + 1] == "{":
                brace_depth += 1
                current.append("${")
                i += 2
                continue
            if ch == "$" and value[i:].startswith("$env{"):
                brace_depth += 1
                current.append("$env{")
                i += 5
                continue
            if ch == "$" and value[i:].startswith("$penv{"):
                brace_depth += 1
                current.append("$penv{")
                i += 6
                continue
            if ch == "}" and brace_depth > 0:
                brace_depth -= 1
                current.append(ch)
                i += 1
                continue
            if ch == sep and brace_depth == 0:
                entries.append("".join(current))
                current = []
                i += 1
                continue
        current.append(ch)
        i += 1
    entries.append("".join(current))
    return entries


def is_env_var_reference(entry, var_name, platform):
    """Whether ``entry`` references the inherited value of ``var_name``."""
    token = str(entry).strip()
    target = var_name.lower() if platform == PLATFORM_WINDOWS else var_name
    for pattern in (
        r"^\$\{env:([^}]+)\}$",
        r"^\$\{env\.([^}]+)\}$",
        r"^\$p?env\{([^}]+)\}$",
        r"^%([^%]+)%$",
        r"^\$\{?([A-Za-z_][A-Za-z0-9_]*)\}?$",
    ):
        match = re.match(pattern, token)
        if match:
            ref_name = match.group(1)
            if (ref_name.lower() if platform == PLATFORM_WINDOWS else ref_name) == target:
                return True
    return False


def extract_third_party_install_prefix(entry, workspace_dir, platform):
    """Return the ``third_party/install/<triplet>`` prefix for workspace-managed entries, else ``None``."""
    expanded = expand_workspace_tokens(entry, workspace_dir, platform)
    if not expanded or "${" in expanded or "$" in expanded or "%" in expanded:
        return None
    expanded_fwd = normalize_platform_path(expanded, platform).replace("\\", "/")
    workspace_fwd = (
        normalize_platform_path(workspace_dir, platform)
        .replace("\\", "/")
        .rstrip("/")
    )
    marker = workspace_fwd + "/third_party/install/"
    cmp_expanded = expanded_fwd.lower() if platform == PLATFORM_WINDOWS else expanded_fwd
    cmp_marker = marker.lower() if platform == PLATFORM_WINDOWS else marker
    if not cmp_expanded.startswith(cmp_marker):
        return None
    remainder = cmp_expanded[len(cmp_marker) :]
    triplet = remainder.split("/", 1)[0]
    if not triplet:
        return None
    return cmp_marker + triplet


def merge_env_path_value(existing_value, desired_dirs, var_name, workspace_dir, platform):
    """Merge ``desired_dirs`` into a PATH-style value idempotently while preserving user order.

    Missing directories are inserted before any earlier managed third-party directory from a
    different toolchain triplet or before the first inherited ``${env:<var_name>}`` reference
    (whichever comes first), keeping user-prepended custom directories at highest priority.
    """
    sep = ";" if platform == PLATFORM_WINDOWS else ":"
    if not desired_dirs:
        return existing_value
    if existing_value is None:
        return sep.join(list(desired_dirs) + ["${env:%s}" % var_name])
    if existing_value.strip() == "":
        return sep.join(desired_dirs)

    entries = split_env_path_entries(existing_value, sep)
    existing_keys = set()
    for entry in entries:
        key = canonical_env_entry_key(entry, workspace_dir, platform)
        if key:
            existing_keys.add(key)

    missing = [
        directory
        for directory in desired_dirs
        if canonical_env_entry_key(directory, workspace_dir, platform) not in existing_keys
    ]
    if not missing:
        return existing_value

    desired_prefixes = set()
    for directory in desired_dirs:
        prefix = extract_third_party_install_prefix(directory, workspace_dir, platform)
        if prefix:
            desired_prefixes.add(prefix)

    insert_index = None
    for index, entry in enumerate(entries):
        if is_env_var_reference(entry, var_name, platform):
            insert_index = index
            break
        entry_prefix = extract_third_party_install_prefix(entry, workspace_dir, platform)
        if entry_prefix and entry_prefix not in desired_prefixes:
            insert_index = index
            break

    if insert_index is not None:
        entries[insert_index:insert_index] = missing
    elif entries and entries[-1] == "":
        entries[-1:-1] = missing
    else:
        entries.extend(missing)
    return sep.join(entries)


def find_env_dict_key(env_dict, var_name, platform):
    """Find an existing env-dict key matching ``var_name`` (case-insensitive on Windows)."""
    if var_name in env_dict:
        return var_name
    if platform == PLATFORM_WINDOWS:
        lowered = var_name.lower()
        for existing_key in env_dict:
            if isinstance(existing_key, str) and existing_key.lower() == lowered:
                return existing_key
    return None


def compute_runtime_env_specs(platform, bin_dirs, lib_dirs, workspace_dir):
    """Build ``[(var_name, formatted_dirs, debug_default_name), ...]`` for ``platform``."""
    if platform == PLATFORM_WINDOWS:
        combined = deduplicate_env_dirs(
            list(bin_dirs) + list(lib_dirs), workspace_dir, platform
        )
        if not combined:
            return []
        # Windows loads DLLs via PATH; cppvsdbg expects PascalCase "Path" when creating a new entry.
        return [("PATH", combined, "Path")]

    formatted_bin = deduplicate_env_dirs(bin_dirs, workspace_dir, platform)
    formatted_lib = deduplicate_env_dirs(lib_dirs, workspace_dir, platform)
    specs = []
    if formatted_bin:
        specs.append(("PATH", formatted_bin, "PATH"))
    if formatted_lib:
        lib_var = (
            "DYLD_FALLBACK_LIBRARY_PATH"
            if platform == PLATFORM_OSX
            else "LD_LIBRARY_PATH"
        )
        specs.append((lib_var, formatted_lib, lib_var))
    return specs


def apply_path_environment_block(data, key, specs, workspace_dir, platform, changes):
    """Merge PATH-like environment variables into a dict-shaped settings block ``key``."""
    if not specs:
        return
    block = data.get(key)
    if block is not None and not isinstance(block, dict):
        print(
            "[optimize-vscode] skip %s: existing value is not an object" % key,
            file=sys.stderr,
        )
        return
    if block is None:
        block = {}

    touched = False
    for var_name, desired_dirs, _debug_name in specs:
        existing_key = find_env_dict_key(block, var_name, platform)
        if existing_key is not None:
            current_value = block[existing_key]
            if current_value is None:
                # In VS Code settings, null explicitly unsets the variable; respect the user.
                continue
            if not isinstance(current_value, str):
                print(
                    "[optimize-vscode] skip %s.%s: existing value is not a string"
                    % (key, existing_key),
                    file=sys.stderr,
                )
                continue
            target_key = existing_key
        else:
            current_value = None
            target_key = var_name

        updated_value = merge_env_path_value(
            current_value, desired_dirs, var_name, workspace_dir, platform
        )
        if updated_value != current_value:
            block[target_key] = updated_value
            touched = True
            action = "add" if current_value is None else "update"
            changes.append("%s: %s %s" % (key, action, target_key))

    if touched or key in data:
        data[key] = block


def apply_debug_config_environment(data, specs, workspace_dir, platform, changes):
    """Merge runtime PATH/library search paths into ``cmake.debugConfig`` without overriding ``type``."""
    if not specs:
        return
    key = "cmake.debugConfig"
    debug_config = data.get(key)
    if debug_config is not None and not isinstance(debug_config, dict):
        print(
            "[optimize-vscode] skip %s: existing value is not an object" % key,
            file=sys.stderr,
        )
        return
    if debug_config is None:
        debug_config = {}

    # CodeLLDB (type == "lldb") uses an "env" object rather than cpptools' "environment" array.
    debug_type = str(debug_config.get("type", "")).strip().lower()
    use_env_dict = debug_type == "lldb" or (
        isinstance(debug_config.get("env"), dict) and "environment" not in debug_config
    )

    if use_env_dict:
        nested_changes = []
        apply_path_environment_block(
            debug_config, "env", specs, workspace_dir, platform, nested_changes
        )
        if nested_changes:
            data[key] = debug_config
            for entry in nested_changes:
                changes.append("%s.%s" % (key, entry))
        return

    env_list = debug_config.get("environment")
    if env_list is not None and not isinstance(env_list, list):
        print(
            "[optimize-vscode] skip %s.environment: existing value is not an array"
            % key,
            file=sys.stderr,
        )
        return
    if env_list is None:
        env_list = []

    touched = False
    for var_name, desired_dirs, debug_default_name in specs:
        matched_entry = None
        for item in env_list:
            if not isinstance(item, dict):
                continue
            item_name = item.get("name")
            if not isinstance(item_name, str):
                continue
            if (
                item_name.lower() == var_name.lower()
                if platform == PLATFORM_WINDOWS
                else item_name == var_name
            ):
                matched_entry = item
                break

        if matched_entry is not None:
            current_value = matched_entry.get("value")
            if current_value is None:
                continue
            if not isinstance(current_value, str):
                print(
                    "[optimize-vscode] skip %s.environment[%s]: existing value is not a string"
                    % (key, matched_entry.get("name")),
                    file=sys.stderr,
                )
                continue
            updated_value = merge_env_path_value(
                current_value, desired_dirs, var_name, workspace_dir, platform
            )
            if updated_value != current_value:
                matched_entry["value"] = updated_value
                touched = True
                changes.append(
                    "%s.environment: update %s" % (key, matched_entry["name"])
                )
        else:
            updated_value = merge_env_path_value(
                None, desired_dirs, var_name, workspace_dir, platform
            )
            env_list.append({"name": debug_default_name, "value": updated_value})
            touched = True
            changes.append("%s.environment: add %s" % (key, debug_default_name))

    if touched:
        debug_config["environment"] = env_list
        data[key] = debug_config


def apply_runtime_path_settings(data, opts, changes):
    """Inject extra third-party bin/lib search paths into terminal, CTest, and debug settings."""
    platform = detect_host_platform(opts.platform)
    specs = compute_runtime_env_specs(
        platform, opts.extra_bin_dirs, opts.extra_lib_dirs, opts.workspace_dir
    )
    if not specs:
        return
    terminal_key = TERMINAL_ENV_SETTING_BY_PLATFORM[platform]
    apply_path_environment_block(
        data, terminal_key, specs, opts.workspace_dir, platform, changes
    )
    apply_path_environment_block(
        data, "cmake.testEnvironment", specs, opts.workspace_dir, platform, changes
    )
    apply_debug_config_environment(data, specs, opts.workspace_dir, platform, changes)


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Optimize .vscode/settings.json for atsf4g-co."
    )
    parser.add_argument(
        "--settings", required=True, help="Path to the workspace .vscode/settings.json"
    )
    parser.add_argument(
        "--workspace-dir",
        required=True,
        help="Workspace root (for ${workspaceFolder} relativization)",
    )
    parser.add_argument(
        "--build-dir",
        default="",
        help="CMake build directory (compile_commands.json location)",
    )
    parser.add_argument(
        "--compiler-id", default="", help="CMAKE_CXX_COMPILER_ID, e.g. MSVC/GNU/Clang"
    )
    parser.add_argument(
        "--compiler-path", default="", help="Absolute path of the C++ compiler"
    )
    parser.add_argument(
        "--cpplint", default="", help="Discovered cpplint executable (hint)"
    )
    parser.add_argument(
        "--clang-format", default="", help="Discovered clang-format executable (hint)"
    )
    parser.add_argument(
        "--protoc", default="", help="Discovered protoc executable (hint)"
    )
    parser.add_argument(
        "--platform",
        choices=SUPPORTED_PLATFORMS,
        default=None,
        help="Target host OS for runtime env settings (default: auto-detect)",
    )
    parser.add_argument(
        "--extra-bin-dir",
        dest="extra_bin_dirs",
        action="append",
        default=[],
        help="Extra third-party bin directory for runtime PATH (repeatable)",
    )
    parser.add_argument(
        "--extra-lib-dir",
        dest="extra_lib_dirs",
        action="append",
        default=[],
        help="Extra third-party lib directory for runtime library search path (repeatable)",
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="Report changes without writing"
    )
    parser.add_argument("--quiet", action="store_true", help="Suppress output")
    return parser.parse_args(argv)


def main(argv):
    opts = parse_args(argv)
    settings_path = os.path.abspath(opts.settings)

    try:
        data, had_comments = load_settings(settings_path)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(
            "[optimize-vscode] failed to read %s: %s" % (settings_path, error),
            file=sys.stderr,
        )
        return 1

    original = copy.deepcopy(data)
    changes = []

    migrate_environment_keys(
        data,
        "cmake.configureEnvironment",
        "cmake.environment",
        MIGRATE_TO_ENVIRONMENT,
        changes,
    )
    apply_environment_block(
        data, "cmake.configureEnvironment", DESIRED_CONFIGURE_ENVIRONMENT, changes
    )
    apply_environment_block(data, "cmake.environment", DESIRED_ENVIRONMENT, changes)
    apply_runtime_path_settings(data, opts, changes)

    # Exclude the build output tree and other heavy/append-only directories from the file
    # watcher and the search scope. This is the primary fix for the editor/save/directory-create
    # latency observed on workspaces whose build tree reaches tens of GB: the build step
    # generates a steady stream of .obj/.pdb/.log change events that would otherwise block the
    # watcher queue on every editor operation. ``files.exclude`` is intentionally NOT touched:
    # it hides entries from the Explorer, which makes browsing the workspace inconvenient.
    build_glob = compute_build_exclude_glob(opts.build_dir, opts.workspace_dir)
    watcher_globs = list(STATIC_EXCLUDE_GLOBS)
    search_globs = list(STATIC_EXCLUDE_GLOBS)
    if build_glob is not None:
        watcher_globs.insert(0, build_glob)
        search_globs.insert(0, build_glob)
    apply_glob_block(data, "files.watcherExclude", watcher_globs, changes)
    apply_glob_block(data, "search.exclude", search_globs, changes)

    # Keep C++/Python static analysis off the generated build tree, merge-only (arrays: existing
    # user entries are preserved). The actual build dir (``--build-dir``) and the generic ``build``
    # output dir are ensured; no broad ``build_*`` wildcard, so ``.../build_setting`` stays analyzed.
    build_dir_glob = compute_build_dir_glob(opts.build_dir, opts.workspace_dir)
    cpplint_excludes = [GENERIC_BUILD_EXCLUDE_GLOB]
    pylint_ignores = ["**/build/**/*.py"]
    analysis_excludes = ["**/build"]
    if build_glob is not None:
        cpplint_excludes.insert(0, build_glob)
    if build_dir_glob is not None:
        pylint_ignores.insert(0, build_dir_glob + "/**/*.py")
        analysis_excludes.insert(0, build_dir_glob)
    apply_list_block(data, "cpplint.excludes", cpplint_excludes, changes)
    apply_list_block(data, "pylint.ignorePatterns", pylint_ignores, changes)
    apply_list_block(data, "python.analysis.exclude", analysis_excludes, changes)

    apply_tool_path(
        data, "cpplint.cpplintPath", opts.cpplint, ["cpplint", "cpplint.exe"], changes
    )

    if is_cmake_format_extension_available():
        apply_tool_path(
            data,
            CMAKE_FORMAT_PATH_SETTING,
            resolve_python_cmake_format(),
            (),
            changes,
        )

    apply_protobuf_protoc_path(data, opts, changes)

    intellisense_disabled = (
        str(data.get("C_Cpp.intelliSenseEngine", "default")).strip().lower()
        == "disabled"
    )
    if not intellisense_disabled:
        apply_tool_path(
            data,
            "C_Cpp.clang_format_path",
            opts.clang_format,
            ["clang-format", "clang-format.exe"],
            changes,
        )

    if is_using_clangd(data, intellisense_disabled):
        apply_clangd_arguments(data, opts, changes)

    changed = json.dumps(original, sort_keys=True, ensure_ascii=False) != json.dumps(
        data, sort_keys=True, ensure_ascii=False
    )

    if not changed:
        if not opts.quiet:
            print("[optimize-vscode] settings.json already optimized; no changes.")
        return 0

    if not opts.quiet:
        print("[optimize-vscode] planned changes for %s:" % settings_path)
    for entry in changes:
        print("  - %s" % entry)

    if opts.dry_run:
        if not opts.quiet:
            print("[optimize-vscode] dry-run: no file written.")
        return 0

    try:
        os.makedirs(os.path.dirname(settings_path), exist_ok=True)
        if os.path.isfile(settings_path):
            shutil.copy2(settings_path, settings_path + ".bak")
        with open(settings_path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(json.dumps(data, indent=2, ensure_ascii=False))
            handle.write("\n")
    except OSError as error:
        print(
            "[optimize-vscode] failed to write %s: %s" % (settings_path, error),
            file=sys.stderr,
        )
        return 1

    if had_comments:
        if not opts.quiet:
            print(
                "[optimize-vscode] WARNING: comments were present and are not preserved; "
                "the original was backed up to settings.json.bak.",
                file=sys.stderr,
            )
    if not opts.quiet:
        print(
            "[optimize-vscode] updated %s (backup: settings.json.bak)." % settings_path
        )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
