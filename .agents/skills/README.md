# Skills (Agent Playbooks)

This is a fallback index for clients that do not natively discover `.agents/skills/`. Native clients should route from
Skill metadata and should not preload this file. Open only the matching `SKILL.md`.

## Contents

| Skill                     | Description                                                                           |
| ------------------------- | ------------------------------------------------------------------------------------- |
| `engineering-guidelines/` | C++/CMake/protobuf conventions, lint review, API/ABI, generated code                  |
| `change-workflow/`        | Defects and nontrivial/high-risk behavior, API, data, security, or deployment changes |
| `build/`                  | CMake configure/build and compile, link, or configure failures                        |
| `testing/`                | Ordinary unit-test targets, test design/review, execution, filters, Windows startup   |
| `rpc-unit-test/`          | `src/**` RPC/runtime/async-hook unit tests and their mock engines                     |
| `matching/`               | Matchmaking behavior across matchsvr/lobbysvr, WAL migration, rules, and tests       |
| `deployment-config/`      | Go deployment templates, Helm values, atdtool rendering, and generated scripts        |
| `configure-expression/`   | `enable_expression` and environment expansion in annotated config fields              |
| `atgateway-protocol/`     | atgateway v2 wire protocol, handshake, crypto, compression, and reconnection          |
| `docs-site/`              | Docusaurus content/build, zh-CN/en i18n, navigation, and theme styling                |
| `ai-agent-maintenance/`   | Repository AI guidance, Skill triggers, progressive disclosure, and bridges           |
| `shell-tooling/`            | Modern CLI tool inventory, shell selection, and PowerShell authoring rules            |

## Maintenance rules

- Folder name and frontmatter `name` must match.
- `description` is always-visible discovery context: front-load user intent, keep it concise, and state the nearest
  useful exclusion when adjacent Skills could match.
- For a new or materially changed trigger description, check representative should-trigger and near-miss
  should-not-trigger requests; narrow false positives instead of adding keyword lists.
- Keep each `SKILL.md` focused and procedural. Put only steps and gotchas needed on every activation in the entrypoint;
  move low-frequency variants into one-level references and state exactly when to read each one.
- Keep compact behavioral guardrails in `AGENTS.md`; do not duplicate the full generic guideline body in every skill.
- Do not duplicate this Skill table in `AGENTS.md`; native clients already expose metadata, while `CLAUDE.md` imports
  this index as its fallback router.
- Keep shared engineering style and review conventions in `engineering-guidelines/SKILL.md`, not in this index; add
  source-of-truth convention changes there before summarizing them elsewhere.
- This index is root-scoped; subprojects maintain independent `.agents/skills/README.md` files.
