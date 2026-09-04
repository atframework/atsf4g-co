# C++ and protobuf style

Detail companion to `SKILL.md`. Load when writing or reviewing C++ or protobuf code under `src/**`.

## Language and naming

- The build requires a C++17-capable toolchain and may select a newer language mode, but new and modified project-owned
  source must stay compatible with C++14. Do not treat the configured language mode or nearby newer code as permission
  to rely on post-C++14 language or standard-library features.
- Do not add post-C++14 syntax or standard-library APIs. Examples include designated initializers, structured bindings,
  `if constexpr`, `<ranges>`/`std::ranges`, `std::optional`, `std::variant`, `std::filesystem`, `std::string_view`, and
  `std::scoped_lock`. Use C++14 loops/algorithms and synchronization types, or established project compatibility APIs
  such as `atfw::util::nostd::string_view`.
- When an external dependency exposes a newer-only API, isolate that integration behind an existing language/version
  feature check and provide an equivalent C++14 branch. Templates that emit C++ and generated-code inputs must obey the
  same rule; do not hand-edit derived output to hide an incompatible template.
- Follow Google C++ Style except that functions and C++ types use STL-like lowercase with underscores; everything else
  (variables, parameters, namespaces, file names, comments) follows Google defaults.
- Do not abbreviate identifiers. Function names, member/global/static variable names, protobuf type names, and
  protobuf field names must use full words. The ban is not limited to a fixed list: any similar shortened form is
  rejected. Examples of banned abbreviations: `pos`, `desc`, `opr`, `cnt`, `par`, `arr`, `buf`, `ptr`, `ts`, `len`,
  `col`, `sz`, `con`, `dec`, `enc`, `in`, `out`, `ver`, and `dir`; write `position`, `description`, `operation`,
  `count`, `parameter`, `array`, `buffer`, `pointer`, `timestamp`, `length`, `column`, `size`, `connection`,
  `decode`/`decoder`, `encode`/`encoder`, `input`, `output`, `version`, and `directory` instead. Temporary stack
  variables are exempt. Widely established project or domain abbreviations that every reader already knows (for
  example `id`, `cfg`, `rpc`, `db`) remain allowed.
- Do not use meaningless names. The ban is not limited to a fixed list: `a`, `b`, `c`, `tmp`, and `temp` are
  examples, and any other name that carries no meaning for its content or purpose is rejected. Conventional short
  loop counters (`i`, `j`, `k`) in small loops are still acceptable.
- New typedefs/aliases commonly use a `*_t` suffix. Preserve existing public names, generated names, protobuf names,
  enum constants, macro-style constants, and ABI/API names instead of renaming for style churn.
- Namespaces follow local project naming; framework code uses `atframework::*`.

## Formatting and files

- `.clang-format` is Google-based with 120-column limit, spaces, left pointer alignment, preserved include blocks,
  sorted includes within each block, and indented preprocessor directives.
- Use `#pragma once` for headers.
- Keep `.editorconfig` rules: LF by default, 120-column default, final newline, trimmed trailing whitespace; Markdown
  and Windows scripts intentionally use CRLF.

## Header and ABI boundaries

- Classify every interface by its publication boundary before choosing an inline/export marker.
- Every externally consumable non-template API of a library must use one of the project's two publication models:
  cover the declaration or enclosing type with that library's `*_API`/`XXX_API` symbol-export macro, or mark the header
  definition `ATFW_UTIL_FORCEINLINE`. Plain `inline`, implicit inline, or `constexpr` alone does not satisfy this rule.
- A public template function defined in a header may instead use `ATFW_UTIL_SYMBOL_VISIBLE`: a non-inlined instantiation
  can share one visible copy across linked targets, while an inlined call embeds a per-target copy. Keep the definition
  ODR-identical for every consumer so those copies have identical behavior.
- For the exported-symbol model, keep non-template function, method, constructor, destructor, and static-data
  implementations in `.cpp` files by default so the ABI stays stable across compilers and build options.
- Never cover a whole **class** with a library `*_API` export macro when it holds members whose types are not
  themselves exported (`std::string`, `std::vector`, `std::shared_ptr`, `std::function`, STL containers in general):
  on MSVC that triggers C4251 (an error under `/WX`), and even where tolerated it makes the DLL ABI depend on the
  exact compiler/STL version of both sides. Export free functions, or export individual member functions, instead.
- Small RAII handles or value structs that must cross the DLL boundary (rule tokens, hook handles) use the handle
  pattern: mark the class `ATFW_UTIL_SYMBOL_VISIBLE`, define every special member and accessor `ATFW_UTIL_FORCEINLINE`
  in the header, and move non-trivial logic into exported free/detail functions. Reference implementation:
  `rpc::unit_test::mock_rule_handle` in `src/server_frame/rpc/unit_test/mock_engine_bridge.h`; applied example:
  `logic_hpa::mock::prometheus_pull_hook_handle` in `src/server_frame/logic/hpa/mock/logic_hpa_mock_prometheus.h`.
- Library-internal helpers, executable-only code, and other interfaces that are not exported may use implicit inline,
  `constexpr`, or explicit `inline` when they satisfy the C++ ODR and match the local design. In this internal-only
  scope, use `ATFW_UTIL_FORCEINLINE` only when local convention or a concrete performance requirement calls for it.

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

- Consume every RPC call's return value by its declared type. APIs returning `rpc::always_ready_code_type` /
  `rpc::always_ready_void_type` (SS `broadcast::*`, CS downstream `send_*`/`broadcast_*`, fire-and-forget calls) must
  be awaited or `.unwrap()`ed: the implicit `operator value_type()` does NOT mark the result awaited, and
  `PROJECT_SERVER_FRAME_*_COROUTINE_CHECK_AWAIT` builds assert in the destructor
  (`src/server_frame/rpc/rpc_common_types.h`). APIs returning `rpc::rpc_result<T>` / `rpc::result_code_type` must be
  awaited; never bind them to a plain value or discard them.
- Await only through the dual-coroutine adapter macros `RPC_AWAIT_CODE_RESULT` / `RPC_AWAIT_TYPE_RESULT` /
  `RPC_AWAIT_IGNORE_RESULT` / `RPC_AWAIT_IGNORE_VOID` (`src/server_frame/rpc/rpc_common_types.h`), which work in both
  C++20 and legacy stackful coroutine builds. Never write `co_await`/`co_yield` directly in project code.
- Lambdas passed to `rpc::async_invoke` must not capture by reference, including default `[&]` and explicit `[&value]`
  captures. The callable runs in an independent task and may execute or resume after the enclosing function has
  returned; copy or move required state into the closure and use an owning smart pointer when pointee lifetime must be
  extended.
- Temporary protobuf scratch messages inside task/RPC code should use the Arena-backed pattern from
  [`rpc-protobuf-arena.md`](rpc-protobuf-arena.md).
- Do not store task-Arena messages in members, globals, caches, or delayed callbacks that may outlive the task/RPC
  context.
