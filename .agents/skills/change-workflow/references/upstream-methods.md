# Upstream Change-Workflow Methods

Use these official sources only when the task concerns current OpenSpec or Superpowers behavior. Both projects are
mutable; verify the live repository, installed integration, profile, and release notes before writing commands or paths.

## Superpowers

- [Repository and basic workflow](https://github.com/obra/superpowers)
- [Releases](https://github.com/obra/superpowers/releases)
- [Brainstorming](https://github.com/obra/superpowers/blob/main/skills/brainstorming/SKILL.md)
- [Test-driven development](https://github.com/obra/superpowers/blob/main/skills/test-driven-development/SKILL.md)
- [Systematic debugging](https://github.com/obra/superpowers/blob/main/skills/systematic-debugging/SKILL.md)
- [Verification before completion](https://github.com/obra/superpowers/blob/main/skills/verification-before-completion/SKILL.md)

The [v7.0.2 brainstorming Skill](https://github.com/obra/superpowers/blob/v7.0.2/skills/brainstorming/SKILL.md)
distinguishes a small change from a project design and offers a disposable
spike path. If adopted, use the installed version for its design, review,
approval, and planning rules. Verify what the harness exposes.

## OpenSpec

- [Repository and current commands](https://github.com/Fission-AI/OpenSpec)
- [Releases](https://github.com/Fission-AI/OpenSpec/releases)
- [Concepts and artifact model](https://github.com/Fission-AI/OpenSpec/blob/main/docs/concepts.md)
- [Existing-project adoption](https://github.com/Fission-AI/OpenSpec/blob/main/docs/existing-projects.md)

OpenSpec supplies a persistent change contract: proposal for intent and scope, delta specs for added/modified/removed
behavior and scenarios, design decisions, ordered tasks, verification, and reconciliation into current specs. If this
repository adopts it, its change is the single source for proposed behavior; Superpowers or the active agent harness may
drive execution without creating a second contract.

The [v2.0.0 release](https://github.com/Fission-AI/OpenSpec/releases/tag/v2.0.0)
moved workflows into Skills. The [v2.2.0 release](https://github.com/Fission-AI/OpenSpec/releases/tag/v2.2.0)
adds a Skill router. Check the generated Skills and active profile before
invoking a workflow. After validation, inspect the actual sync or archive
diff and verify behavior separately from spec structure.

## Local boundary

- No `openspec/` or verified Superpowers integration means use `change-workflow/SKILL.md` and existing repository
  artifacts; do not invent slash commands, initialize directories, or claim either tool was used.
- Small, clear edits take the shortest verified path. Persistent contracts and design gates are for work whose risk,
  ambiguity, review, or handoff cost justifies them.
