# AI customization sources

Use this reference when a change affects discovery, client precedence,
invocation policy, or the MCP integration. The AI guidance maintainer
rechecks the affected row before changing it and after an upgrade,
deprecation, or loading failure. The version column refers to published
docs or the named spec revision. Local probes and limits are recorded in
the [coverage record](initialization-coverage.md#客户端检查).

## Rule and Skill discovery

The following claims were checked against official documentation on
2026-11-28. They describe documented discovery, not execution in this
checkout. A Markdown link provides a path for manual reading.

| Surface | Version and source | Documented behavior and affected file |
| --- | --- | --- |
| Agent Skills | Rolling [specification][skills-spec], [authoring][skills-author] and [evaluation][skills-eval] | Portable frontmatter has two required and four optional fields. Discovery, selected execution, and output evaluation require separate checks. See the [maintenance Skill](../SKILL.md). |
| Codex | Rolling [project rules][codex-rules] and [Skills][codex-skills] | Each directory uses its first applicable instruction file. Repo Skills are discovered under `.agents/skills/`. The initial catalog may shorten descriptions or omit Skills. Check from root and a relevant subdirectory. |
| Claude Code | Rolling [project memory][claude-rules] and [Skills][claude-skills] | Native `AGENTS.md` starts at 2.1.277. The default setting gives precedence to project `CLAUDE.md` and `CLAUDE.local.md`. The [bridge](../../../../CLAUDE.md) imports the root guide. Skills can use directory symlinks under `.claude/skills/`; check deduplication before adding any. |
| VS Code | Rolling [instructions][vscode-rules], [Skills][vscode-skills] and [prompt files][vscode-prompts] | The selected harness determines discovery. The Copilot harness uses `.github/skills/`; Claude uses `.claude/skills/`; Codex discovers `.agents/skills/` and also accepts workspace `.github/skills/` in its experimental integration. The deprecated Local harness reads legacy prompt files and is scheduled for removal in February 2027. The Git hook prompt under `ci/` is independent. |
| Kilo Code | Rolling [project rules][kilo-rules] and [Skills][kilo-skills] | Uses `AGENTS.md` and discovers project `.agents/skills/`. Memory Bank is deprecated. Verify override precedence and whether the selected Skill location can run embedded commands. |
| Pi | Rolling [Skills][pi-skills] and [configuration][pi-config] | Discovers project `.agents/skills/` to the repo root. Verify project trust, command exposure, and reload behavior in the installed release. |
| Oh My Pi | Main-branch [context files][omp-rules] and [Skills][omp-skills] | Native `.omp/` scope and Skill precedence need their own checks. A valid path or identical Skill entry does not establish which one is selected. |
| Devin | Rolling [Devin Local customizations][devin-local] | Current Local guidance documents hierarchical `AGENTS.md`. Use the Local/CLI sources if a Devin-specific integration is adopted. |

For any client bridge, check the actual harness, trust, project scope,
versions, and same-name overrides. Start a clean session at the root and
the affected subdirectory. Inspect loaded rules and listing output, then
exercise a small allowed invocation. In Claude Code, check `/memory` and
`/context` for loaded instructions. Audit local overrides, auto memory,
and old hooks before migrating an existing setup. Keep team rules in Git.

Other client paths require evidence of use before creating any new
configuration. If needed, start with the official docs for
[OpenCode](https://opencode.ai/docs/skills),
[Command Code](https://commandcode.ai/docs/skills),
[Zoo Code](https://docs.zoocode.dev/features/custom-instructions),
[OpenClaw](https://docs.openclaw.ai/tools/skills),
[Hermes Agent](https://hermes-agent.nousresearch.com/docs/user-guide/features/skills),
and [Antigravity](https://antigravity.google/docs/skills).

## MCP

The official [version history][mcp-version] lists 2026-07-28 as current on
2026-11-28. The [core][mcp-core] uses per-request protocol metadata and
`server/discover`. Check backward compatibility with clients that use the
legacy `initialize` exchange. The project pins
`@modelcontextprotocol/server` 2.4.0 in its
[lockfile](../../../../project/integration/mcp/common/package-lock.json)
and wraps the stdio transport. The spec and lockfile were inspected for
this refresh. No protocol change or client interoperability run was done.

- [stdio binding][mcp-stdio]: reserve stdout for protocol messages and
  use request cancellation without stopping other clients' work.
- [Tool metadata][mcp-tools] and [security guidance][mcp-security]:
  validate inputs and destinations, inspect changes to tool metadata,
  and enforce permissions independently of tool annotations.
- For a future HTTP binding, check the version header, Origin, listener
  scope, and [MCP authorization][mcp-auth]. Verify token audience and
  issuer and prevent token passthrough. Retain stdio credentials in
  the authorized process environment.

[skills-spec]: https://agentskills.io/specification
[skills-author]: https://agentskills.io/skill-creation/best-practices
[skills-eval]: https://agentskills.io/skill-creation/evaluating-skills
[codex-rules]: https://learn.chatgpt.com/docs/agent-configuration/agents-md
[codex-skills]: https://learn.chatgpt.com/docs/build-skills
[claude-rules]: https://code.claude.com/docs/en/memory
[claude-skills]: https://code.claude.com/docs/en/skills
[vscode-rules]: https://code.visualstudio.com/docs/agent-customization/custom-instructions
[vscode-skills]: https://code.visualstudio.com/docs/agent-customization/agent-skills
[vscode-prompts]: https://code.visualstudio.com/docs/agent-customization/prompt-files
[kilo-rules]: https://kilo.ai/docs/customize/agents-md
[kilo-skills]: https://kilo.ai/docs/customize/skills
[pi-skills]: https://docs.pi.dev/latest/skills
[pi-config]: https://docs.pi.dev/latest/configuration
[omp-rules]: https://github.com/can1357/oh-my-pi/blob/main/docs/context-files.md
[omp-skills]: https://github.com/can1357/oh-my-pi/blob/main/docs/skills.md
[devin-local]: https://docs.devin.ai/desktop/devin-local/customizations
[mcp-version]: https://modelcontextprotocol.io/docs/2026-07-28/learn/versioning
[mcp-core]: https://modelcontextprotocol.io/specification/2026-07-28/basic/index
[mcp-stdio]: https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/stdio
[mcp-tools]: https://modelcontextprotocol.io/specification/2026-07-28/server/tools
[mcp-security]: https://modelcontextprotocol.io/docs/2026-07-28/tutorials/security/security_best_practices
[mcp-auth]: https://modelcontextprotocol.io/specification/2026-07-28/basic/authorization
