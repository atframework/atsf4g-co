# Review checklist and validation

Detail companion to `SKILL.md`. Load when reviewing a change or finalizing edits before reporting.

## Before editing

- Inspect current file contents and current diffs; preserve unrelated user or formatter edits.
- Read nearest instructions for the path being changed, especially vendored subproject `AGENTS.md` files.
- Identify whether a generated source, template, protocol, or helper rule is the real source of truth.

## While writing or reviewing

- Check naming, ownership/lifetime, return-code handling, logging, include wrappers, generated-code boundaries, and
  helper usage in the same pass.
- Prefer existing helper APIs, project error codes, and local patterns over ad hoc alternatives.
- Keep comments useful and current; remove placeholder `TODO` text when implementing the placeholder.

## Markdown and prompt files

- Keep prompt/skill bridge files concise; route to the engineering-guidelines skill instead of duplicating rules.
- For Markdown, `.editorconfig` uses CRLF, no max line length, and keeps trailing whitespace for `*.md` and `*.md.in`.

## C++ format and lint

- Run `clang-format -i` on modified C/C++ files, then `clang-format --dry-run --Werror` on the same files.
- Run configured `cpplint` and `clang-tidy` for touched files when practical; use the current build directory and
  compile database for `clang-tidy`.
- Expected result: no warnings and no required formatting changes.

## CMake format and lint

- Run `cmake-format` on modified `.cmake`, `.cmake.in`, and `CMakeLists.txt` files.
- Run `cmake-lint` when available. Expected result: no warnings.

## Markdown and prompt validation

- Run `markdownlint` when available; if no repository-specific markdownlint config exists, use tool defaults plus
  `.editorconfig` expectations.
- Always check YAML frontmatter, heading order, whitespace, and line endings for changed prompt/skill files.

## Reporting

- If a validation tool is unavailable or too expensive for the change, explicitly report what was skipped and why.
