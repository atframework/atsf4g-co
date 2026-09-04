#!/usr/bin/env python3
# Copyright 2026 atframework
# Licensed under the Apache License, Version 2.0 (the "License");

"""Prepare a clang-tidy-only compilation database."""

import argparse
import ctypes
import json
import os
import shlex
import sys
from pathlib import Path


def _split_windows_command_line(command):
    argument_count = ctypes.c_int()
    command_line_to_argv = ctypes.windll.shell32.CommandLineToArgvW
    command_line_to_argv.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    command_line_to_argv.restype = ctypes.POINTER(ctypes.c_wchar_p)
    local_free = ctypes.windll.kernel32.LocalFree
    local_free.argtypes = [ctypes.c_void_p]
    local_free.restype = ctypes.c_void_p
    argument_values = command_line_to_argv(command, ctypes.byref(argument_count))
    if not argument_values:
        raise ctypes.WinError()
    try:
        return [argument_values[index] for index in range(argument_count.value)]
    finally:
        local_free(argument_values)


def _split_command_line(command):
    if os.name == "nt":
        return _split_windows_command_line(command)
    return shlex.split(command, posix=True)


def _msvc_option(argument):
    lower_argument = argument.lower()
    for option in ("/yu", "/yc", "/fp", "/fi"):
        if lower_argument == option:
            return option, None
        if lower_argument.startswith(option):
            return option, argument[len(option) :]
    return None, None


def _is_cmake_pch_header(value):
    if not value:
        return False
    basename = value.replace("\\", "/").rsplit("/", 1)[-1].lower()
    return basename in ("cmake_pch.h", "cmake_pch.hxx")


def _has_cmake_msvc_pch(arguments):
    index = 1
    while index < len(arguments):
        option, value = _msvc_option(arguments[index])
        if option in ("/yu", "/yc", "/fi"):
            if value is None and index + 1 < len(arguments):
                value = arguments[index + 1]
            if _is_cmake_pch_header(value):
                return True
        index += 1
    return False


def _remove_cmake_msvc_pch(arguments):
    if not _has_cmake_msvc_pch(arguments):
        return arguments, 0

    result = arguments[:1]
    removed_count = 0
    index = 1
    while index < len(arguments):
        argument = arguments[index]
        option, value = _msvc_option(argument)
        consumes_value = value is None and option is not None and index + 1 < len(arguments)
        option_value = arguments[index + 1] if consumes_value else value
        remove_argument = option == "/fp" or (
            option in ("/yu", "/yc", "/fi") and _is_cmake_pch_header(option_value)
        )
        if remove_argument:
            removed_count += 1 + int(consumes_value)
            index += 2 if consumes_value else 1
            continue
        result.append(argument)
        index += 1
    return result, removed_count


def _entry_arguments(entry):
    arguments = entry.get("arguments")
    if isinstance(arguments, list) and all(isinstance(argument, str) for argument in arguments):
        return arguments.copy()
    command = entry.get("command")
    if not isinstance(command, str):
        raise ValueError("each compilation database entry must contain a string command or arguments array")
    return _split_command_line(command)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--remove-cmake-msvc-pch", action="store_true")
    arguments = parser.parse_args()

    with arguments.input.open("r", encoding="utf-8-sig") as input_file:
        database = json.load(input_file)
    if not isinstance(database, list):
        raise ValueError("the compilation database root must be an array")

    removed_count = 0
    prepared_database = []
    for raw_entry in database:
        if not isinstance(raw_entry, dict):
            raise ValueError("each compilation database entry must be an object")
        entry = raw_entry.copy()
        entry_arguments = _entry_arguments(entry)
        if arguments.remove_cmake_msvc_pch:
            entry_arguments, entry_removed_count = _remove_cmake_msvc_pch(entry_arguments)
            removed_count += entry_removed_count
        entry.pop("command", None)
        entry["arguments"] = entry_arguments
        prepared_database.append(entry)

    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    temporary_output = arguments.output.with_name(f"{arguments.output.name}.tmp")
    with temporary_output.open("w", encoding="utf-8", newline="\n") as output_file:
        json.dump(prepared_database, output_file, ensure_ascii=False, indent=2)
        output_file.write("\n")
    os.replace(temporary_output, arguments.output)
    print(
        f"clang-tidy: prepared {len(prepared_database)} compilation database entries; "
        f"removed {removed_count} CMake MSVC PCH argument(s)."
    )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"clang-tidy: failed to prepare compilation database: {error}", file=sys.stderr)
        sys.exit(2)
