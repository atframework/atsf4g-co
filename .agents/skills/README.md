# Skills (Agent Playbooks)

This folder is a compact index of repository workflows that agents load on demand. Keep `AGENTS.md` and this index
small; put task-specific steps, commands, caveats, and examples in each `SKILL.md`.

## Contents

| Skill                     | Description                                                                              |
| ------------------------- | ---------------------------------------------------------------------------------------- |
| `engineering-guidelines/` | Code style, lint, RPC/Arena, code generation, service/component CMake, review/validation |
| `build/`                  | Configure and build the project (Windows / Unix)                                         |
| `testing/`                | Run unit tests (including Windows DLL/PATH notes)                                        |
| `deployment-config/`      | Generate and manage deployment configs/scripts                                           |
| `configure-expression/`   | Environment-variable expression expansion in configuration fields                        |
| `atgateway-protocol/`     | atgateway v2 protocol SDK — ECDH handshake, encryption, testing                          |
| `orbit/`                  | Orbit protocol notes, pseudocode, and flow design                                        |
| `ai-agent-maintenance/`   | Audit and optimize AI agent prompts, bridge files, and skills                            |

## Maintenance rules

- Folder name and frontmatter `name` must match.
- `description` is the discovery surface: start with `Use when:` and include concrete trigger words.
- Keep each `SKILL.md` focused; move bulky examples or reference material into sibling files when needed.
- Keep shared engineering style and review conventions in `engineering-guidelines/SKILL.md`, not in this index.
