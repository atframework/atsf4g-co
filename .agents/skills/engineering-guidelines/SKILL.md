---
name: engineering-guidelines
description: "Use when: writing or reviewing C++/CMake/Markdown code, applying project engineering conventions, checking header inline and exported API ABI rules, cpplint, clang-format, cmake-format, cmake-lint, markdownlint, clang-tidy, protobuf, RPC, generated-code, service/component CMake rules."
---

# Engineering guidelines

Use this skill before writing or reviewing code in this repository. This file is a compact router. Load only the
sibling section you need so unrelated rules do not bloat your context.

## Sections

| File                                                     | Load when                                                  |
| -------------------------------------------------------- | ---------------------------------------------------------- |
| [cpp-style.md](cpp-style.md)                             | Writing/reviewing C++, protobuf, header inline, or API ABI  |
| [cmake-and-generated.md](cmake-and-generated.md)         | Editing CMake, `.proto`, templates, or generated code      |
| [service-functions-cmake.md](service-functions-cmake.md) | Service/component/protocol/SDK CMake helpers               |
| [code-generation.md](code-generation.md)                 | `.proto`, Mako templates, generated task/RPC code          |
| [rpc-protobuf-arena.md](rpc-protobuf-arena.md)           | Task/RPC temporary protobuf allocation                     |
| [review-and-validation.md](review-and-validation.md)     | Reviewing a change or running format/lint before reporting |

## Scope and source of truth

- Root project (`src/**`): use repository configs `.clang-format`, `.clang-tidy`, `.editorconfig`, `CPPLINT.cfg`,
  `.cmake-format.yaml`. Prefer small, focused changes; avoid unrelated cleanup or broad reformatting.
- Vendored subprojects (`atframework/**`): read the nearest subproject `AGENTS.md` first and use its own configs when
  they differ from the root.
- Protobuf definitions, Mako templates, and CMake generation rules are sources of truth; regenerate rather than
  hand-edit derived outputs.

## Related standalone skills

These cover environment, domain, or workflow knowledge rather than coding norms; load only when the task area is active.

| Skill                              | Load when                                                               |
| ---------------------------------- | ----------------------------------------------------------------------- |
| `../build/SKILL.md`                | Configuring/building or fixing build failures                           |
| `../testing/SKILL.md`              | Unit tests, test filters, Windows DLL/PATH startup                      |
| `../atgateway-protocol/SKILL.md`   | atgateway v2 protocol, crypto, compression, reconnection, gateway tests |
| `../deployment-config/SKILL.md`    | Deployment configs, generated scripts, Helm values                      |
| `../configure-expression/SKILL.md` | Env-expression-enabled config fields                                    |
| `../orbit/SKILL.md`                | Orbit protocol notes, SDK/session routing pseudocode, flow design       |
