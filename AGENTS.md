# atsf4g-co Agent Guide

This is the canonical cross-agent guide for this repository. Keep repeatable workflows in `.agents/skills/`, keep
compatibility bridges thin, and avoid copying Skill bodies into always-on prompts. Subprojects under `atframework/`
maintain independent guidance; use the nearest `AGENTS.md` inside them.

**atsf4g-co** (AT Service Framework for Game - Coroutine) is a service framework for building game servers with
libatbus, libatapp, libcopp, and other atframework components. It provides a complete solution for scalable,
high-performance game server architectures.

- **Repository**: <https://github.com/atframework/atsf4g-co>
- **Languages**: C++ (C++17-capable build toolchain; new and modified project-owned C++ uses a C++14-compatible subset)

## Project Map

- `src/server_frame/`: shared config, protocol, dispatcher, router, RPC, data, and utility code.
- `src/*svr/`: service implementations (`echosvr`, `authsvr`, `cachesvr`, `lobbysvr`, `rank_settlement_svr`, `orbitsvr`).
- `src/component/`: DTMQ, distributed transaction, rank, orbit, and shared algorithm components plus their SDKs.
- `src/robot/` and `src/tools/`: Go stress client, generators, diagnostics, and the offline mock-RPC test fixture.
- `src/templates/`: Mako templates for generated RPC/task code.
- `doc/`: Docusaurus documentation site (architecture + development docs, zh-CN/en i18n).
- `atframework/`: vendored framework repositories; `project/`, `third_party/`, `install/`, and `resource/` hold build,
  dependency, deployment, and runtime assets.
- Main flow: `Client -> atgateway -> atproxy -> service -> dispatcher -> logic -> data/DB`.
- Protobuf definitions and templates are the source of truth; generated files should normally be regenerated, not edited.

## Always-On Rules

- Respect the user's dirty workspace: inspect current file contents before editing and avoid unrelated reformatting.
- Write comments, docs, and agent guidance for their intended reader, in the file's own language. Use familiar, literal
  terms and keep established technical terms when they are exact. For example, name the actual 缓存/副本/派生视图 instead
  of calling it 投影, and name the actual action instead of using 补强. Start with the fact or action, keep one idea per
  sentence, and remove repeated setup, conclusions, vague praise, and formulaic label-plus-explanation lists. Treat
  suspicious words as editing signals, not a blacklist; replace a term only when it is vague, inaccurate, redundant,
  or unfamiliar to the intended reader.
- Start with the current task, nearest instructions, and capabilities exposed by the active harness. Use exposed Skill
  metadata for routing; consult `.agents/skills/README.md` only when the harness lacks discovery. Load a `SKILL.md` only
  after the task matches it, and load its references only under their stated conditions.
- If the Project Map and matching Skill do not identify the authoritative evidence, read the on-demand
  [source index](.agents/source-index.md). Do not preload it or scan the full documentation tree.
- Before a nontrivial plan or edit, inspect the relevant code, configs, docs, generated sources, tests, and current
  official docs for mutable external behavior. Separate verified facts from assumptions, then state the smallest plan
  and verification path; do not invent behavior from memory.
- Keep new and modified project-owned C++ in this root repository source-compatible with C++14, including tests and
  templates that emit C++. A build configured for a newer standard does not permit post-C++14 syntax or standard-library
  APIs. Use existing compatibility wrappers or C++14 forms; if dependency integration requires a newer facility,
  isolate it behind a standard-version guard and keep an equivalent C++14 path. See `engineering-guidelines` for
  examples and review checks.
- For test work, derive cases from verified behavior and risk. Exercise a contract-valid workflow with deterministic
  controls, then cover the highest-value boundaries and failures. Do not invent interfaces, fields, environments, or
  root causes; do not shape data only to make a test pass or add redundant cases for counts. When a check fails, fix
  the implementation or the case design; never weaken, skip, or loosen an existing assertion, add a retry, or widen a
  timeout merely to force green. If scope or prerequisites remain unclear, state assumptions and coverage gaps instead
  of claiming completeness.
- Match process to risk: use the shortest verified path for small changes; read `change-workflow` for defects and for
  cross-module behavior, public API/ABI, data model/migration, security, or deployment changes. Keep their scope and
  acceptance in one existing authoritative artifact or active task plan; do not initialize a methodology for ceremony.
- Resolve `<BUILD_DIR>` before creating build trees or temporary files: read `.vscode/settings.json` for
  `cmake.buildDirectory`; if it is absent, infer from `clangd.arguments` `--compile-commands-dir=...` or an existing
  configured build tree; if no user setting is readable, use `build`.
- Keep build trees and temporary/script/log output under `<BUILD_DIR>/`; use `<BUILD_DIR>/_agent_tmp/` for agent scratch.
  Before CMake commands, reuse the workspace generator, configure options, and parallelism.
- For paths under vendored subprojects, read the nearest subproject `AGENTS.md` before changing code.

## Skill Routing

Use the Skill metadata exposed by the harness. If it exposes none, read the compact
[Skill index](.agents/skills/README.md), then open only the matching `SKILL.md`.

## Agent File Compatibility

- `AGENTS.md` is canonical for tools that support hierarchical agent instructions.
- `.agents/skills/` is the portable project Skill location. Metadata is the discovery surface; bodies and references
  are on-demand context.
- Do not maintain `.github/copilot-instructions.md` copies when `AGENTS.md` and `.agents/skills/` cover the same rules.
- `CLAUDE.md` imports this guide and the compact fallback index; it must not duplicate either body.
- Do not make this root guide depend on subproject prompt files, and do not make subprojects depend on this file.
- Keep skill folder names and frontmatter `name` values identical; descriptions are the discovery surface.

## Subprojects

Each subproject has its own `AGENTS.md`:

- [atframe_utils/AGENTS.md](atframework/atframe_utils/AGENTS.md)
- [libatbus/AGENTS.md](atframework/libatbus/AGENTS.md)
- [libatapp/AGENTS.md](atframework/libatapp/AGENTS.md)
- [cmake-toolset/AGENTS.md](atframework/cmake-toolset/AGENTS.md)
