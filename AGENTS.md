# atsf4g-co Agent Guide

This is the canonical, self-contained cross-agent guide for this repository. Keep it short: put repeatable workflows in
`.agents/skills/*/SKILL.md`, keep `CLAUDE.md` as a lightweight bridge, and avoid redundant tool-specific prompt copies.
Subprojects under `atframework/` maintain their own independent agent guides and skills; use the nearest subproject
`AGENTS.md` when working inside those repositories.

**atsf4g-co** (AT Service Framework for Game - Coroutine) is a service framework for building game servers with
libatbus, libatapp, libcopp, and other atframework components. It provides a complete solution for scalable,
high-performance game server architectures.

- **Repository**: <https://github.com/atframework/atsf4g-co>
- **Languages**: C++ (C++17 required, C++17/C++20/C++23 features used when available)

## Project Map

- `atframework/`: vendored framework libraries (`atframe_utils`, `libatbus`, `libatapp`, gateway/proxy components).
- `src/server_frame/`: shared config, protocol, dispatcher, router, RPC, data, and utility code.
- `src/*svr/`: service implementations (`echosvr`, `authsvr`, `cachesvr`, `lobbysvr`, `rank_settlement_svr`, `orbitsvr`).
- `src/component/`: shared service components: `dtmq/` (distributed message queue: `dtmq-proxysvr` + client SDK + WAL
  replication), `distributed_transaction/` (2PC coordinator `dtcoordsvr` + SDK), `rank/` (`rank_board_svr` + SDK),
  `orbit/` (controller/agent/server SDK), `GameSharedComponent/` (pure algorithm libs).
- `src/robot/`: Go stress-test robot client; `src/tools/`: code generator (`generate-for-pb`), etcd debug tools.
- `src/templates/`: Mako templates for generated RPC/task code.
- `doc/`: Docusaurus documentation site (architecture + development docs, zh-CN/en i18n).
- `project/`, `third_party/`, `install/`, `resource/`: build tooling, dependency setup, deployment assets, resources.
- `install/**/*.tpl`: Go `text/template` deployment templates. When a second suffix exists before `.tpl`, use it as
  the rendered target syntax (`.yaml.tpl` -> YAML, `.sh.tpl` -> shell, `.bat.tpl` -> Windows batch); bare `.tpl` files
  are usually shared helper/partial templates.
- Main flow: `Client → atgateway → atproxy → service → dispatcher → logic → data/DB`.
- `atgateway` handles client connections, ECDH/DH handshakes, encryption, compression, and routing handoff.
- `atproxy` handles cross-service communication, discovery, and online detection.
- Protobuf definitions and templates are the source of truth; generated files should normally be regenerated, not edited.

## Always-On Rules

- Respect the user's dirty workspace: inspect current file contents before editing and avoid unrelated reformatting.
- Start with the current task, nearest instructions, Skill index, and capabilities actually exposed by the active agent
  harness. Load full Skill bodies or tool-specific directories only when the task routes there; do not assume or install
  absent workflows, tools, modes, or extensions.
- Before a nontrivial plan or edit, inspect the relevant code, configs, docs, generated sources, tests, and current
  official docs for mutable external behavior. Separate verified facts from assumptions, then state the smallest plan
  and verification path; do not invent behavior from memory.
- Match process to risk: use the shortest verified path for small changes; read `change-workflow` for defects and for
  cross-module behavior, public API/ABI, data model/migration, security, or deployment changes. Keep their scope and
  acceptance in one existing authoritative artifact or active task plan; do not initialize a methodology for ceremony.
- Never unconditionally `touch` or same-content overwrite code/resources consumed by `add_custom_command`,
  `add_custom_target`, `add_executable`, `add_library`, `target_sources`, or another dependency edge, whether generated,
  copied, or non-handwritten. Use content-stable writes and accurate `OUTPUT`/`BYPRODUCTS`/`DEPENDS`/`DEPFILE`; only
  dedicated non-consumed stamp files may be touched.
- Resolve `<BUILD_DIR>` before creating build trees or temporary files: read `.vscode/settings.json` for
  `cmake.buildDirectory`; if it is absent, infer from `clangd.arguments` `--compile-commands-dir=...` or an existing
  configured build tree; if no user setting is readable, use `build`.
- Put all CMake build trees, AI scratch files, script output/logs, and temporary data under `<BUILD_DIR>/...`; for agent
  scratch use `<BUILD_DIR>/_agent_tmp/...`. Never create ad-hoc `tmp/`, `log/`, `startup*.log`, generated scripts, or
  diagnostic dumps in the repository root.
- Before running CMake configure/build/test commands, align with the resolved `<BUILD_DIR>`, generator, configure
  settings, and parallel-job settings from `.vscode/settings.json` when present.
- For paths under vendored subprojects, read the nearest subproject `AGENTS.md` before changing code.
- When a task matches a skill below, read that `SKILL.md` first; skills contain the long commands and edge cases.
- For code analysis or edits under `install/**/*.tpl`, read `deployment-config` first; analyze both Go-template actions
  and the rendered target language, and validate target syntax only after rendering representative output.
- For coding or code review in `src/**`, first read `engineering-guidelines`; it owns shared style, configured
  `.clang-tidy`/`CPPLINT.cfg` review checks, header/template visibility and API ABI boundaries, and project conventions.
- CMake target names must not hardcode the project prefix: use `${PROJECT_NAME}-<name>` (or the established
  `atframework` prefix where applicable), never literal `atf4g-co-...`/`atf4g-co::...`, in any target declaration,
  alias, `if(TARGET ...)`, dependency, folder, or label.

## Skill Routing

Read the matching `.agents/skills/*/SKILL.md` before doing specialized work:

| Skill                     | Use when                                                                                |
| ------------------------- | --------------------------------------------------------------------------------------- |
| `engineering-guidelines`  | Writing/reviewing C++/CMake/Markdown, header inline/template visibility/API ABI rules, RPC/Arena |
| `change-workflow`         | Diagnosing defects or delivering nontrivial/high-risk changes with a reviewable contract       |
| `build`                   | Configuring or building with CMake                                                      |
| `testing`                 | Running or writing unit tests                                                           |
| `deployment-config`       | Generating/editing deployment configs, Go `.tpl` chart templates, or Helm values        |
| `configure-expression`    | Editing env-expression-enabled config fields                                            |
| `atgateway-protocol`      | Working on atgateway v2 protocol, crypto, compression, reconnection, or tests           |
| `docs-site`               | Building or editing the Docusaurus site under `doc/` (i18n zh-CN/en, fonts)              |
| `ai-agent-maintenance`    | Auditing or optimizing AI agent prompts, bridge files, and skills                       |

## Agent File Compatibility

- `AGENTS.md` is canonical for tools that support hierarchical agent instructions.
- `.agents/skills/` is the portable project skill location; keep each `SKILL.md` focused and self-contained.
- Do not maintain `.github/copilot-instructions.md` copies when `AGENTS.md` and `.agents/skills/` cover the same rules.
- `CLAUDE.md` exists only to point Claude-compatible tools at this guide and `.agents/skills/`.
- Do not make this root guide depend on subproject prompt files, and do not make subprojects depend on this file.
- Keep skill folder names and frontmatter `name` values identical; descriptions are the discovery surface.

## Subprojects

Each subproject has its own `AGENTS.md`:

- [atframe_utils/AGENTS.md](atframework/atframe_utils/AGENTS.md)
- [libatbus/AGENTS.md](atframework/libatbus/AGENTS.md)
- [libatapp/AGENTS.md](atframework/libatapp/AGENTS.md)
- [cmake-toolset/AGENTS.md](atframework/cmake-toolset/AGENTS.md)
