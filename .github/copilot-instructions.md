# atsf4g-co - VS Code Copilot Notes

<!-- The main project instructions are in AGENTS.md at the repository root. -->
<!-- This file contains only VS Code Copilot-specific notes (skills, path-specific references). -->
<!-- VS Code Copilot reads both this file and the root AGENTS.md automatically. -->

## Skills (How-to playbooks)

Operational, copy/paste-friendly guides live in `.agents/skills/`:

- Entry point: `.agents/skills/README.md`

| Skill              | Path                                              | Description                                           |
| ------------------ | ------------------------------------------------- | ----------------------------------------------------- |
| Build              | `.agents/skills/build/SKILL.md`                   | Configure and build (Windows / Unix)                  |
| Testing            | `.agents/skills/testing/SKILL.md`                 | Run unit tests (including Windows DLL/PATH notes)     |
| Service CMake      | `.agents/skills/service-functions-cmake/SKILL.md` | `src/service-functions.cmake` helper functions        |
| Deployment         | `.agents/skills/deployment-config/SKILL.md`       | Generate and manage deployment configs/scripts        |
| Config Expressions | `.agents/skills/configure-expression/SKILL.md`    | Environment-variable expression expansion             |
| Code Generation    | `.agents/skills/code-generation/SKILL.md`         | Protobuf/template-based code generation               |
| Gateway Protocol   | `.agents/skills/atgateway-protocol/SKILL.md`      | atgateway v2 protocol SDK — ECDH, encryption, testing |

## Subproject Instructions

Each subproject has its own `AGENTS.md` and `.github/copilot-instructions.md`:

- [atframe_utils](../atframework/atframe_utils/.github/copilot-instructions.md)
- [libatbus](../atframework/libatbus/.github/copilot-instructions.md)
- [libatapp](../atframework/libatapp/.github/copilot-instructions.md)
