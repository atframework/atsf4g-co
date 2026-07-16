# Skills (Agent Playbooks)

This folder is a compact index of repository workflows that agents load on demand. Keep `AGENTS.md` and this index
small; put task-specific steps, commands, caveats, and examples in each `SKILL.md`.

## Contents

| Skill                     | Description                                                                               |
| ------------------------- | ----------------------------------------------------------------------------------------- |
| `engineering-guidelines/` | Code style, template visibility/API ABI, configured lint review, RPC/Arena, generated code |
| `change-workflow/`        | Risk-scaled design, change contracts, debugging, TDD, review, and completion evidence      |
| `build/`                  | Configure and build the project (Windows / Unix)                                          |
| `testing/`                | Run unit tests (including Windows DLL/PATH notes)                                         |
| `deployment-config/`      | Generate deployment configs/scripts and analyze `install/**/*.tpl` Go templates           |
| `configure-expression/`   | Environment-variable expression expansion in configuration fields                         |
| `atgateway-protocol/`     | atgateway v2 protocol SDK — ECDH handshake, encryption, testing                           |
| `ai-agent-maintenance/`   | Audit and optimize AI agent prompts, bridge files, and skills                             |

## Maintenance rules

- Folder name and frontmatter `name` must match.
- `description` is the discovery surface: start with `Use when:` and include concrete trigger words.
- For a new or materially changed trigger description, check representative should-trigger and near-miss
  should-not-trigger requests; narrow false positives instead of adding keyword lists.
- Keep each `SKILL.md` focused and procedural. Use progressive disclosure: put only core steps, gotchas, and validation
  in `SKILL.md`, and move bulky examples or reference material into sibling files with clear load conditions.
- Keep compact behavioral guardrails in `AGENTS.md`; do not duplicate the full generic guideline body in every skill.
- Keep shared engineering style and review conventions in `engineering-guidelines/SKILL.md`, not in this index; add
  source-of-truth convention changes there before summarizing them elsewhere.
- This index is root-scoped; subprojects maintain independent `.agents/skills/README.md` files.
