# Unit testing (atsf4g-co)

This repo uses a **private unit testing framework** (shared by atframe_utils/libatbus/libatapp).

## Discover and run tests (generic)

Most test executables support:

- List test cases: `-l` / `--list-tests`
- Run by group or case: `-r <group>` or `-r <group>.<case>`
- Filter (wildcards): `-f "pattern*"` / `--filter "pattern*"`
- Help/version: `-h`, `-v`

## Windows: DLL lookup via PATH

On Windows, unit tests/samples can fail to start if dependent DLLs are not found.

Preferred approach: **prepend DLL directories to `PATH`** for the current run/debug session.

Typical DLL directories in this repo:

- `<BUILD_DIR>\\publish\\bin\\<Config>` (project DLLs)
- `<REPO_ROOT>\\third_party\\install\\windows-amd64-msvc-19\\bin` (third-party DLLs when using the bundled cmake-toolset)

Example (PowerShell):

- Set PATH and run `atframe_utils_unit_test.exe`:
  - `$buildDir = "<BUILD_DIR>"`
  - `$cfg = "Debug"`
  - `$env:PATH = "$buildDir\\publish\\bin\\$cfg;$buildDir\\publish\\bin;${PWD}\\third_party\\install\\windows-amd64-msvc-19\\bin;" + $env:PATH`
  - `Set-Location "$buildDir\\_deps\\atframe_utils\\test\\$cfg"`
  - `./atframe_utils_unit_test.exe -l`

Example (cmd.exe):

- `set "BUILD_DIR=<BUILD_DIR>"`
- `set "CFG=Debug"`
- `set "PATH=%BUILD_DIR%\\publish\\bin\\%CFG%;%BUILD_DIR%\\publish\\bin;%CD%\\third_party\\install\\windows-amd64-msvc-19\\bin;%PATH%"`
- `cd /d "%BUILD_DIR%\\_deps\\atframe_utils\\test\\%CFG%"`
- `atframe_utils_unit_test.exe -l`

## Tip: using VS Code tasks

If the workspace provides CMake build tasks, prefer those to ensure UTF-8 console output and consistent build directories.
