---
name: engineering-guidelines
description: "Use when: writing or reviewing C++, CMake, protobuf, RPC, or generated-code inputs, including repo lint rules and API/ABI conventions. Do not use for docs-only, build execution, or test execution."
---

# Engineering guidelines

Use this Skill before writing or reviewing production code or generated-code inputs. It is a compact router: load only
the sibling section needed for the current task.

## Sections

| File                                                     | Load when                                                  |
| -------------------------------------------------------- | ---------------------------------------------------------- |
| [cpp-style.md](cpp-style.md)                             | C++, protobuf, async lambda lifetime, header/API ABI        |
| [cmake-and-generated.md](cmake-and-generated.md)         | Editing CMake, `.proto`, templates, or generated code      |
| [service-functions-cmake.md](service-functions-cmake.md) | Service/component/protocol/SDK CMake helpers               |
| [code-generation.md](code-generation.md)                 | `.proto`, Mako templates, generated task/RPC code          |
| [rpc-protobuf-arena.md](rpc-protobuf-arena.md)           | Task/RPC temporary protobuf allocation                     |
| [review-and-validation.md](review-and-validation.md)     | Reviewing changes, checking configured lint issues, or final validation |

## Scope and source of truth

- Root project (`src/**`): use repository configs `.clang-format`, `.clang-tidy`, `.editorconfig`, `CPPLINT.cfg`,
  `.cmake-format.yaml`. Prefer small, focused changes; avoid unrelated cleanup or broad reformatting.
- Code review findings must be backed by the current nearest config or observable project convention. If a clang-tidy
  or cpplint rule is disabled by config, report it only as a clearly labeled design suggestion, not as a lint violation.
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
