# atsf4g-co Agent Guide

This is the canonical, cross-agent guide for this repository. Keep it short: put repeatable workflows in
`.agents/skills/*/SKILL.md`, and keep `.github/copilot-instructions.md` / `CLAUDE.md` as lightweight bridges.

**atsf4g-co** (AT Service Framework for Game - Coroutine) is a service framework for building game servers with
libatbus, libatapp, libcopp, and other atframework components. It provides a complete solution for scalable,
high-performance game server architectures.

- **Repository**: <https://github.com/atframework/atsf4g-co>
- **Languages**: C++ (C++17 required, C++17/C++20/C++23 features used when available)

## Project Map

- `atframework/`: vendored framework libraries (`atframe_utils`, `libatbus`, `libatapp`, gateway/proxy components).
- `src/server_frame/`: shared config, protocol, dispatcher, router, RPC, data, and utility code.
- `src/*svr/` and `src/component/`: service implementations and shared service components.
- `src/templates/`: Mako templates for generated RPC/task code.
- `project/`, `third_party/`, `install/`, `resource/`: build tooling, dependency setup, deployment assets, resources.
- Main flow: `Client → atgateway → atproxy → service → dispatcher → logic → data/DB`.
- `atgateway` handles client connections, ECDH/DH handshakes, encryption, compression, and routing handoff.
- `atproxy` handles cross-service communication, discovery, and online detection.
- Protobuf definitions and templates are the source of truth; generated files should normally be regenerated, not edited.

## Always-On Rules

- Respect the user's dirty workspace: inspect current file contents before editing and avoid unrelated reformatting.
- For paths under vendored subprojects, read the nearest subproject `AGENTS.md` before changing code.
- When a task matches a skill below, read that `SKILL.md` first; skills contain the long commands and edge cases.
- For coding or code review in `src/**`, first read `engineering-guidelines`; it owns shared style, lint, and project
  engineering conventions.

## Skill Routing

Read the matching `.agents/skills/*/SKILL.md` before doing specialized work:

| Skill                     | Use when                                                                                |
| ------------------------- | --------------------------------------------------------------------------------------- |
| `engineering-guidelines`  | Writing or reviewing C++/CMake/Markdown code, RPC/Arena, generated code, service CMake  |
| `build`                   | Configuring or building with CMake                                                      |
| `testing`                 | Running or writing unit tests                                                           |
| `deployment-config`       | Generating/editing deployment configs or Helm values                                    |
| `configure-expression`    | Editing env-expression-enabled config fields                                            |
| `atgateway-protocol`      | Working on atgateway v2 protocol, crypto, compression, reconnection, or tests           |
| `orbit`                   | Drafting or reviewing orbit protocol notes, pseudocode, and flow design                 |
| `ai-agent-maintenance`    | Auditing or optimizing AI agent prompts, bridge files, and skills                       |

## Agent File Compatibility

- `AGENTS.md` is canonical for tools that support hierarchical agent instructions.
- `.github/copilot-instructions.md` exists only to point VS Code Copilot at this guide and `.agents/skills/`.
- `CLAUDE.md` exists only to point Claude-compatible tools at this guide and `.agents/skills/`.
- Keep skill folder names and frontmatter `name` values identical; descriptions are the discovery surface.

## Subprojects

Each subproject has its own `AGENTS.md`:

- [atframe_utils/AGENTS.md](atframework/atframe_utils/AGENTS.md)
- [libatbus/AGENTS.md](atframework/libatbus/AGENTS.md)
- [libatapp/AGENTS.md](atframework/libatapp/AGENTS.md)
