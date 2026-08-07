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
* ``C_Cpp.clang_format_path`` is filled or repaired the same way, but only when
  ``C_Cpp.intelliSenseEngine`` is not ``disabled``.
* When clangd is in use, ``clangd.arguments`` is normalized:
    - ``--compile-commands-dir=<build dir>`` is appended only when absent.
    - ``--query-driver`` is repaired to the real MSVC driver (MSVC compilers only).
    - ``--background-index`` and ``--clang-tidy`` are ensured.
    - ``--header-insertion`` is forced to ``never`` (the only value clangd accepts;
      note that ``Never`` / ``iwyu`` casing variants are rejected by clangd).

JSONC comments are not preserved on rewrite; a ``.bak`` copy is written before any change.
"""

import argparse
import copy
import json
import os
import re
import shutil
import sys

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


def compute_compile_commands_dir(build_dir, workspace_dir):
    """Prefer a ``${workspaceFolder}``-relative value, else an absolute path."""
    build_abs = os.path.abspath(build_dir)
    workspace_abs = os.path.abspath(workspace_dir)
    try:
        rel = os.path.relpath(build_abs, workspace_abs)
    except ValueError:
        rel = None
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
        print(
            "[optimize-vscode] %s not found; leaving %s unchanged" % (names[0], key),
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
    apply_tool_path(
        data, "cpplint.cpplintPath", opts.cpplint, ["cpplint", "cpplint.exe"], changes
    )

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
