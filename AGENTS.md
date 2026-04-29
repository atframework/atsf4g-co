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
- After C++ edits, run `clang-format -i <file>` and verify with `clang-format --dry-run --Werror <file>` when practical.

## C++ Conventions

1. **Namespaces**:
   - Framework: `atframework::*`
   - Project: Follow project naming

2. **Include guards**: Use `#pragma once`

3. **C++ Standard**: C++17 required

4. **Naming**:
   - Classes/structs: `snake_case`
   - Functions: `snake_case`
   - Constants: `UPPER_SNAKE_CASE`
   - Types: `*_t` suffix for typedefs

5. **Error handling**: Use return codes or error enums

6. **Logging**: Use FWLOG macros

   ```cpp
   FWLOGINFO("Message: {}", value);
   FWLOGERROR("Error: {}", error);
   ```

7. **Anonymous namespace + static**: In `.cpp` files, file-local functions should be placed inside an anonymous namespace **and** keep the `static` keyword. Do **not** remove `static` when moving a function into an anonymous namespace.

   ```cpp
   namespace {
   static void my_helper() { /* ... */ }
   }  // namespace
   ```

8. **Temporary protobuf messages in tasks/RPC APIs**: When creating a protobuf message object only as a temporary inside
   a task or RPC interface, and it is not cached, stored as an object member, or kept beyond the task/RPC lifetime,
   prefer `rpc::make_shared_message<MessageType>(ctx)` (or
   `rpc::make_shared_message<MessageType>(get_shared_context())` inside `task_action_*`) over stack/heap allocation.
   This allocates from the task/RPC Arena and reduces heap fragmentation. Use `msg->...`, `*msg`, and `msg.get()` as
   appropriate; include `rpc/rpc_shared_message.h` when needed. Do not use task-Arena objects for data that must outlive
   the Arena.

## Skill Routing

Read the matching `.agents/skills/*/SKILL.md` before doing specialized work:

| Skill | Use when |
| --- | --- |
| `build` | Configuring or building with CMake |
| `testing` | Running or writing unit tests |
| `service-functions-cmake` | Adding services, protocols, SDKs, or components |
| `deployment-config` | Generating/editing deployment configs or Helm values |
| `configure-expression` | Editing env-expression-enabled config fields |
| `code-generation` | Editing proto files, templates, or generated outputs |
| `rpc-protobuf-arena` | Working with task/RPC-local protobuf messages and Arena allocation |
| `atgateway-protocol` | Working on atgateway v2 protocol, crypto, compression, reconnection, or tests |
| `ai-agent-maintenance` | Auditing or optimizing AI agent prompts, bridge files, and skills |

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
