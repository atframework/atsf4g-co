# C++ and protobuf style

Detail companion to `SKILL.md`. Load when writing or reviewing C++ or protobuf code under `src/**`.

## Language and naming

- C++17 is required. Use C++20/C++23 features only when surrounding code and build options already allow them.
- Follow Google C++ Style except that functions and C++ types use STL-like lowercase with underscores; everything else
  (variables, parameters, namespaces, file names, comments) follows Google defaults.
- New typedefs/aliases commonly use a `*_t` suffix. Preserve existing public names, generated names, protobuf names,
  enum constants, macro-style constants, and ABI/API names instead of renaming for style churn.
- Namespaces follow local project naming; framework code uses `atframework::*`.

## Formatting and files

- `.clang-format` is Google-based with 120-column limit, spaces, left pointer alignment, preserved include blocks,
  sorted includes within each block, and indented preprocessor directives.
- Use `#pragma once` for headers.
- Keep `.editorconfig` rules: LF by default, 120-column default, final newline, trimmed trailing whitespace; Markdown
  and Windows scripts intentionally use CRLF.

## Includes and protobuf wrappers

- Wrap every upstream protobuf header and generated `*.pb.h` include between `config/compiler/protobuf_prefix.h` and
  `config/compiler/protobuf_suffix.h`.
- Disable clang-format only around the prefix/suffix wrapper includes, not around unrelated project includes.

## Runtime conventions

- Prefer project return codes or error enums over exceptions unless local code already uses exceptions.
- Logging: when an `rpc::context` is reachable in the current scope (directly, via `get_shared_context()`, or through a
  callee parameter), prefer `FCTXLOG...` macros so logs carry trace/span context. Use `FWLOG...` macros only when no
  `rpc::context` is available. Always use structured format strings.
- File-local helpers in `.cpp` files should live in an anonymous namespace and keep the `static` keyword.
- Remove stale unused markers such as `ATFW_EXPLICIT_UNUSED_ATTR` only when the symbol is actually used; keep them for
  template placeholders and intentionally unused parameters.

## RPC/protobuf lifetimes

- Temporary protobuf scratch messages inside task/RPC code should use the Arena-backed pattern from
  [`rpc-protobuf-arena.md`](rpc-protobuf-arena.md).
- Do not store task-Arena messages in members, globals, caches, or delayed callbacks that may outlive the task/RPC
  context.
