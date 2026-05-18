---
name: ai-agent-maintenance
description: "Use when: auditing or optimizing AI agent prompts, bridge files, skills, SKILL.md metadata, and cross-tool compatibility."
---

# AI Agent Maintenance

Use this skill when updating AI-agent guidance for this repository or subproject.

## Required Outcomes

- Deeply research the current prompt/skill layout and current AI-agent customization practices before editing.
- Keep always-on guidance compact, actionable, and non-redundant.
- Keep engineering style, lint, and project coding conventions in `../engineering-guidelines/SKILL.md`; bridge files and
  this maintenance skill should only route to that skill.
- Keep `.agents/skills/README.md` as a compact index; do not turn it into a second copy of skill bodies.
- Keep temporary-artifact guidance consistent: AI-created scratch files and script/log output should go in ignored build
  subdirectories, not repository roots.
- Preserve compatibility across AGENTS-aware tools, VS Code Copilot, Codex, Claude Code, Kilo Code/CLI, Roo Code,
  Windsurf, Antigravity, OpenClaw/Hermes-style skills, and OpenCode where the repository intentionally supports them.
- Keep root and subproject AI surfaces independently maintainable; never require a parent or sibling repository prompt
  file for this repository's guidance to make sense.
- Re-check and record official source URLs whenever compatibility behavior, skill locations, or frontmatter semantics
  are changed.
- Merge improvements into existing prompt and skill content; do not leave old versions, migration notes, changelog notes,
  or historical comparison sections.

## Compatibility Model

- Prefer `AGENTS.md` as the canonical repository or subproject guide; nested `AGENTS.md` files should be specific to the
  subtree they govern.
- Keep `.agents/skills/<name>/SKILL.md` as the portable skill source for VS Code, Roo/Kilo/Windsurf/Antigravity-style
  skill discovery, and OpenClaw/Hermes external skill directories.
- Do not maintain `.github/copilot-instructions.md` copies when `AGENTS.md` and `.agents/skills/` cover the same rules.
- Keep `CLAUDE.md` as a thin Claude Code bridge that imports `AGENTS.md` and the skills index with `@...` references;
  prefer imports over symlinks for Windows compatibility.
- Keep repeatable workflows in `.agents/skills/<name>/SKILL.md`; bridge files should not duplicate full skill bodies.
- Do not mirror `.agents/skills` workflows into `.claude/skills`; keep `CLAUDE.md` as the Claude-compatible bridge.
- For Roo/Kilo/Windsurf/OpenCode/Antigravity/OpenClaw compatibility, preserve `AGENTS.md` and `.agents/skills/` first.
  Add tool-specific folders or config (`.roo`, `.kilo`, `.windsurf`, `.opencode`, etc.) only when the
  repository already uses them or the task explicitly asks for that surface.
- Do not commit MCP server secrets, OAuth tokens, or local runtime configuration. If MCP guidance is needed, document
  least-privilege tool exposure, explicit user consent, timeouts, and secret handling instead of checked-in credentials.

## Procedure

### Research first

- Read the nearest `AGENTS.md`, `CLAUDE.md`, `.agents/skills/README.md`, and any relevant `SKILL.md` files before
  editing. Read legacy `.github` AI customization files only when migrating or deleting them.
- Check the nearest `.gitignore` and existing build-directory names before changing temp-file guidance so examples point
  to ignored build subdirectories that already exist or are safe to create.
- For code style or engineering-convention changes, also read `../engineering-guidelines/SKILL.md` and merge updates
  there instead of duplicating rules in `AGENTS.md` or specialized skills.
- If compatibility behavior may change, check current official docs or maintained references for the affected tools.
- Capture the URLs or local docs consulted in the final summary, and add a compact source note only when it helps future
  maintainers avoid repeating the same research.
- Respect dirty workspaces: preserve unrelated user or formatter edits and avoid broad reformatting.

### Choose the right surface

- Put facts that apply to nearly every task in `AGENTS.md`.
- Repository-wide temporary-artifact placement rules belong in `AGENTS.md`; skills should explain how to preserve the
  rule, not replace it.
- Put shared coding/review conventions in `engineering-guidelines`; keep specialized skills focused on detailed workflows
  and edge cases.
- Put only names, one-line descriptions, and maintenance rules in `.agents/skills/README.md`.
- Put path-specific or tool-specific rules in their native file only when that scope is needed.
- Put multi-step, task-specific, or rarely used guidance in skills.
- Prefer links to existing docs or skills over copying long reference material into always-on prompts.

### Write compact, discoverable skills

- Keep skill folder names and frontmatter `name` values identical; use lowercase hyphenated names.
- Quote descriptions that contain colons and start them with `Use when:` plus concrete trigger words.
- Front-load the most important trigger phrases; some tools truncate skill descriptions in listings.
- Keep each `SKILL.md` focused. Move bulky examples, scripts, or reference material into sibling files when needed.

### Example policy pattern

- Prefer examples like `build/_agent_tmp/<task>/notes.txt`, `build/_agent_tmp/<task>/script.log`, or an existing ignored
  build tree such as `build_jobs_cmake_tools/_agent_tmp/`.
- If no build tree exists yet, tell the agent to create `build/_agent_tmp/` (or the nearest repo's ignored `build/`
  subdirectory) instead of root-level `tmp/`, `log/`, or ad-hoc debug files.

### Validate before finishing

- Check markdown/frontmatter diagnostics for changed prompt and skill files.
- Run a scoped whitespace check for changed prompt and skill files.
- Re-read representative files to ensure bridge files stay thin and skill routing points to the current skill.
- For nested Git repositories, run status and whitespace checks from each affected repository root.

### Summarize clearly

- Report the files changed, compatibility surfaces preserved, and validations run.
- Call out skipped build/test work when the change is documentation-only.

## Source Links to Re-check

- AGENTS.md guidance: <https://agents.md/>
- Agent Skills specification and best practices: <https://agentskills.io/specification>
- VS Code Copilot custom instructions and skills: <https://code.visualstudio.com/docs/copilot/customization/overview>
- Claude Code memory and skills: <https://docs.anthropic.com/claude-code/>
- MCP security and tool design: <https://modelcontextprotocol.io/docs/>
- Roo/Kilo/Windsurf/OpenCode/OpenClaw/Hermes/Antigravity docs when touching those compatibility surfaces.
