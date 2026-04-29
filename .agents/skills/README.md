# Skills (Agent Playbooks)

This folder contains repository workflows that agents load on demand. Keep `AGENTS.md` small; put task-specific steps,
commands, caveats, and examples here.

## Contents

| Skill | Description |
| --- | --- |
| `build/` | Configure and build the project (Windows / Unix) |
| `testing/` | Run unit tests (including Windows DLL/PATH notes) |
| `service-functions-cmake/` | How to use `src/service-functions.cmake` helper functions |
| `deployment-config/` | Generate and manage deployment configs/scripts |
| `configure-expression/` | Environment-variable expression expansion in configuration fields |
| `code-generation/` | Protobuf/template-based code generation notes |
| `rpc-protobuf-arena/` | Arena-backed temporary protobuf messages in task/RPC code |
| `atgateway-protocol/` | atgateway v2 protocol SDK — ECDH handshake, encryption, testing |
| `ai-agent-maintenance/` | Audit and optimize AI agent prompts, bridge files, and skills |

## When to read what

- If you want to **build**: start with `build/SKILL.md`.
- If you want to **run unit tests**: start with `testing/SKILL.md`.
- If you are adding a new service/protocol/SDK: see `service-functions-cmake/SKILL.md`.
- If you are creating temporary protobuf messages in task/RPC code: see `rpc-protobuf-arena/SKILL.md`.
- If you are working on gateway protocol code: see `atgateway-protocol/SKILL.md`.
- If you are updating AI agent prompts or skills: see `ai-agent-maintenance/SKILL.md`.

## Maintenance rules

- Folder name and frontmatter `name` must match.
- `description` is the discovery surface: start with `Use when:` and include concrete trigger words.
- Keep each `SKILL.md` focused; move bulky examples or reference material into sibling files when needed.
