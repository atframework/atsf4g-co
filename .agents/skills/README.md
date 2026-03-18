# Skills (Agent Playbooks)

This folder contains **actionable, copy/paste-friendly** guides for common workflows in this repository.
Each skill is a directory containing a `SKILL.md` file with YAML frontmatter (following the [Agent Skills](https://agentskills.io/) spec).

## Contents

| Skill | Description |
|-------|-------------|
| `build/` | Configure and build the project (Windows / Unix) |
| `testing/` | Run unit tests (including Windows DLL/PATH notes) |
| `service-functions-cmake/` | How to use `src/service-functions.cmake` helper functions |
| `deployment-config/` | Generate and manage deployment configs/scripts |
| `configure-expression/` | Environment-variable expression expansion in configuration fields |
| `code-generation/` | Protobuf/template-based code generation notes |
| `atgateway-protocol/` | atgateway v2 protocol SDK — ECDH handshake, encryption, testing |

## When to read what

- If you want to **build**: start with `build/SKILL.md`.
- If you want to **run unit tests**: start with `testing/SKILL.md`.
- If you are adding a new service/protocol/SDK: see `service-functions-cmake/SKILL.md`.
- If you are working on gateway protocol code: see `atgateway-protocol/SKILL.md`.
