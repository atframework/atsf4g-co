#!/usr/bin/env python3
# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

"""Translate .clangd clang-tidy fragments into a per-file clang-tidy manifest.

clang-tidy does not read .clangd configuration files, so the analysis driver
scripts (clang-tidy.sh/clang-tidy.ps1) call this helper to keep the custom
target consistent with the editor behavior. For every input file the manifest
records whether clang-tidy should run and which extra check globs to append
after the repository .clang-tidy rules.

Only Diagnostics.ClangTidy.Add/Remove are translated. clangd-only settings
such as FastCheckFilter or CheckOptions are not applied. PathMatch and
PathExclude regexes are matched against the repository-relative path with
forward slashes.
"""

import argparse
import os
import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    yaml = None


def _as_string_list(value):
    if isinstance(value, str):
        return [value]
    if isinstance(value, (list, tuple)):
        return [item for item in value if isinstance(item, str)]
    return []


def _load_fragments(config_path):
    if yaml is None:
        print(
            "clang-tidy: warning: PyYAML is not available for this Python interpreter; "
            ".clangd rules are ignored.",
            file=sys.stderr,
        )
        return []
    try:
        with config_path.open("r", encoding="utf-8-sig") as config_file:
            documents = list(yaml.safe_load_all(config_file))
    except (OSError, yaml.YAMLError) as error:
        print(
            f"clang-tidy: warning: failed to parse {config_path}: {error}; .clangd rules are ignored.",
            file=sys.stderr,
        )
        return []
    return [document for document in documents if isinstance(document, dict)]


def _compile_patterns(patterns, config_path):
    compiled = []
    for pattern in patterns:
        try:
            compiled.append(re.compile(pattern))
        except re.error as error:
            print(
                f"clang-tidy: warning: invalid If regex '{pattern}' in {config_path}: {error}; "
                "the pattern is ignored.",
                file=sys.stderr,
            )
    return compiled


def _fragment_applies(fragment, relative_path, config_path):
    condition = fragment.get("If")
    if condition is None:
        return True
    if not isinstance(condition, dict):
        return False
    path_match = _compile_patterns(
        _as_string_list(condition.get("PathMatch")), config_path
    )
    path_exclude = _compile_patterns(
        _as_string_list(condition.get("PathExclude")), config_path
    )
    if path_match and not any(pattern.search(relative_path) for pattern in path_match):
        return False
    if any(pattern.search(relative_path) for pattern in path_exclude):
        return False
    return True


def _evaluate_file(fragments, relative_path, config_path):
    extra_globs = []
    all_checks_removed = False
    for fragment in fragments:
        if not _fragment_applies(fragment, relative_path, config_path):
            continue
        diagnostics = fragment.get("Diagnostics")
        if not isinstance(diagnostics, dict):
            continue
        tidy = diagnostics.get("ClangTidy")
        if not isinstance(tidy, dict):
            continue
        for pattern in _as_string_list(tidy.get("Add")):
            extra_globs.append(pattern)
            all_checks_removed = False
        for pattern in _as_string_list(tidy.get("Remove")):
            extra_globs.append("-" + pattern.lstrip("-"))
            if pattern.lstrip("-") == "*":
                all_checks_removed = True
    if all_checks_removed:
        return "skip", ""
    return "analyze", ",".join(extra_globs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clangd-config", required=True, type=Path)
    parser.add_argument(
        "--files",
        required=True,
        type=Path,
        help="newline-separated list of repository-relative paths to evaluate",
    )
    parser.add_argument("--manifest", required=True, type=Path)
    arguments = parser.parse_args()

    with arguments.files.open("r", encoding="utf-8-sig") as files_file:
        relative_paths = [line.strip().replace("\\", "/") for line in files_file]
    relative_paths = [path for path in relative_paths if path]
    for relative_path in relative_paths:
        if "\t" in relative_path:
            raise ValueError(
                f"paths with tab characters are not supported: {relative_path!r}"
            )

    fragments = _load_fragments(arguments.clangd_config)

    analyzed_count = 0
    skipped_count = 0
    manifest_lines = []
    for relative_path in relative_paths:
        action, extra_checks = _evaluate_file(
            fragments, relative_path, arguments.clangd_config
        )
        if action == "skip":
            skipped_count += 1
        else:
            analyzed_count += 1
        manifest_lines.append(f"{relative_path}\t{action}\t{extra_checks}")

    arguments.manifest.parent.mkdir(parents=True, exist_ok=True)
    temporary_manifest = arguments.manifest.with_name(f"{arguments.manifest.name}.tmp")
    with temporary_manifest.open("w", encoding="utf-8", newline="\n") as manifest_file:
        for line in manifest_lines:
            manifest_file.write(f"{line}\n")
    os.replace(temporary_manifest, arguments.manifest)
    print(
        f"clang-tidy: applied .clangd rules to {len(relative_paths)} file(s); "
        f"{analyzed_count} to analyze, {skipped_count} skipped (all checks removed)."
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"clang-tidy: failed to evaluate .clangd rules: {error}", file=sys.stderr)
        sys.exit(2)
