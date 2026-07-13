# Review checklist and validation

Detail companion to `SKILL.md`. Load when reviewing a change or finalizing edits before reporting.

## Before editing

- Inspect current file contents and current diffs; preserve unrelated user or formatter edits.
- Read nearest instructions for the path being changed, especially vendored subproject `AGENTS.md` files.
- Identify whether a generated source, template, protocol, or helper rule is the real source of truth.
- Before any code review or nontrivial plan, do a source-of-truth pass: read the touched code, nearest agent guidance,
  relevant generated/template inputs, and the active style/lint configs before writing findings.

## While writing or reviewing

- Check naming, ownership/lifetime, return-code handling, logging, include wrappers, generated-code boundaries,
  header inline/API ABI rules, and helper usage in the same pass.
- Prefer existing helper APIs, project error codes, and local patterns over ad hoc alternatives.
- Keep comments useful and current; remove placeholder `TODO` text when implementing the placeholder.
- Treat unconditional touch or same-content overwrite of target-consumed code/resources as a blocking build-performance
  defect, including generated and copied files. Require content-stable publication plus accurate
  `OUTPUT`/`BYPRODUCTS`/`DEPENDS`/`DEPFILE`; allow touch only on a dedicated non-consumed stamp.
- For C/C++ review findings, parse the current nearest `.clang-tidy` and `CPPLINT.cfg` instead of relying on memory.
  Cite issues only when they are active in config or supported by local project convention.
- Copyright headers are a required review item when `CPPLINT.cfg` does not filter `legal/copyright`; new or modified
  C/C++ files should keep the project copyright header style used by nearby files.
- Include review must check self-header inclusion, system/project include grouping and `.clang-format` sorting within
  preserved blocks, protobuf prefix/suffix wrappers, and obvious missing or stale includes. In the root project,
  `.clang-tidy` disables `misc-include-cleaner`, so do not report include-cleaner-only guesses as lint violations unless
  compile evidence or project wrapper/style rules support them.
- For root `.clang-tidy`, pay attention to the enabled diagnostic/analyzer, `bugprone`, `misc`, `modernize`,
  `cppcoreguidelines`, `performance`, `portability`, and `readability` families, while honoring every explicit disabled
  check in the config.

## Markdown and prompt files

- Keep prompt/skill bridge files concise; route to the engineering-guidelines skill instead of duplicating rules.
- For Markdown, `.editorconfig` uses CRLF, no max line length, and keeps trailing whitespace for `*.md` and `*.md.in`.

## C++ format and lint

- Run `clang-format -i` on modified C/C++ files, then `clang-format --dry-run --Werror` on the same files.
- Run configured `cpplint` and `clang-tidy` for touched files when practical; use the current build directory and
  compile database for `clang-tidy`.
- When tools are too expensive to run during review, still read `.clang-tidy` and `CPPLINT.cfg` and apply their active
  checks manually to common issues such as copyright headers, include hygiene, unsafe casts/conversions, lifetime,
  readability, portability, and performance regressions.
- For touched headers, verify every in-header function body uses `ATFW_UTIL_FORCEINLINE` and no `*_API` exported
  interface is implemented in the header.
- Expected result: no warnings and no required formatting changes.

## CMake format and lint

- Run `cmake-format` on modified `.cmake`, `.cmake.in`, and `CMakeLists.txt` files.
- Run `cmake-lint` when available. Expected result: no warnings.
- For changed generation rules, trace each output to its consuming target and confirm a no-op rerun cannot change the
  timestamp of compiled or packaged code/resources.

## Markdown and prompt validation

- Run `markdownlint` when available; if no repository-specific markdownlint config exists, use tool defaults plus
  `.editorconfig` expectations.
- Always check YAML frontmatter, heading order, whitespace, and line endings for changed prompt/skill files.

## Reporting

- If a validation tool is unavailable or too expensive for the change, explicitly report what was skipped and why.
