---
name: ai-agent-maintenance
description: "Use when: auditing or optimizing repository AI guidance, Agent Skills, trigger descriptions, progressive disclosure, or compatibility bridges. Do not use for normal code changes under existing guidance."
---

# AI Agent Maintenance

Use this Skill for AI-facing surfaces in the `atsf4g-co` root repository. For an independent repository under
`atframework/`, load that repository's local maintenance Skill instead.

## Outcome

- Keep AI-facing guidance source-backed, compact, task-routed, and independently usable in this repository.
- Merge improvements into the current authoritative files; remove stale, duplicated, or historical variants.
- Preserve the user's dirty workspace and avoid tool-specific configuration without a verified need.

## Workflow

### 1. Discover before loading

- Inspect the active harness's actual Skills, tools, modes, permissions, and extensions. Do not assume or install absent
  capabilities.
- Read repository status, the nearest `AGENTS.md`, and the Skill metadata already exposed by the harness. Read a bridge
  or fallback index only when auditing that surface or when native Skill discovery is absent.
- Read `.agents/source-index.md` only when auditing source routing or when the task crosses unfamiliar subsystems and
  the matching Skill does not identify the authoritative files. Do not import it from a compatibility bridge.
- Inventory entrypoint size, trigger overlap, and referenced paths before opening bodies. Open only candidates with a
  relevant trigger, suspected duplication, oversized entrypoint, or broken reference; do not preload every Skill.

### 2. Verify facts and choose process strength

- For initialization or a full guidance refresh, read the complete inputs,
  including the end marker and maintenance instructions. Update the
  [coverage record](references/initialization-coverage.md) before editing.
  Map each applicable requirement to an owning section and a check. Keep
  applicability, implementation, and runtime verification separate.
- Separate stable repository facts from mutable external behavior. Verify the former in current code/config/tests and the
  latter in current official documentation before writing compatibility or security claims.
- Treat OpenSpec and Superpowers as optional, mutable integrations. Verify repository adoption and the active harness's
  current capabilities before referencing their artifacts or commands; never install or initialize them implicitly.
- State assumptions, the smallest sufficient plan, rollback, and validation after the source pass.
- Use the shortest verified path for small, low-risk edits. Require reviewable scope and acceptance criteria before
  cross-module behavior, public API, data model, security, or deployment changes; reuse existing project artifacts rather
  than initializing a methodology for ceremony.

### 3. Choose one authoritative surface

- Put stable, high-signal, broadly applicable rules in `AGENTS.md`.
- Keep `CLAUDE.md` as a thin import of the root guide. Point to the fallback
  Skill index for on-demand reading. Check discovery and override rules before
  adding a bridge. Prefer a verified directory symlink over copying a shared
  Skill when a client needs a separate discovery directory.
- Keep `.agents/skills/README.md` to names, one-line routing, and compact maintenance rules.
- Keep `.agents/source-index.md` as an on-demand, current-state task-to-source map. Point to existing catalogs such as
  `doc/sidebars.js` instead of duplicating every document; do not turn it into a history log or an always-loaded prompt.
- Put repeatable, specialized procedures and non-obvious project knowledge in `.agents/skills/<name>/SKILL.md`.
- Do not repeat a full Skill routing table in `AGENTS.md` when native clients already expose metadata. Keep one compact
  index only for bridges or clients that lack discovery.
- Add a client-specific role only for a persistent need such as restricted
  tools, isolated context, or a handoff. Verify the selected harness and
  schema. Enforce access boundaries through permissions. When delegation
  is authorized, give the subtask its inputs, constraints, file ownership,
  acceptance checks, and required result summary. Review results in the parent.
- Enforce mandatory checks and sensitive-path restrictions in the harness,
  hooks, or CI where supported. Scope hooks to the requirement, keep them
  non-interactive, and verify success and failure exit codes.

### 4. Maintain Skills for low context cost

- Match the folder and `name` using 1–64 lowercase ASCII letters, digits,
  and hyphens. Avoid leading, trailing, or consecutive hyphens. Require
  `name` and `description`; the portable optional fields are `license`,
  `compatibility` (up to 500 characters), `metadata` (string pairs), and
  experimental `allowed-tools`. Keep extensions in client-owned surfaces.
  Distinguish explicit invocation, automatic selection, and execution access.
- Make `description` intent-first and specific: say what the Skill does, when it should trigger, and a useful near-miss
  boundary. Keep the key trigger early and stay within 1024 characters.
- Follow the root writing rule. Write imperative, project-specific procedures, remove explanations the agent already
  knows, use one clear default, and make control stricter only where the workflow is fragile.
- Treat 500 lines and about 5,000 tokens as ceilings, not targets. Keep the entrypoint much smaller when possible; move
  low-frequency APIs, examples, platform branches, and diagnostics to directly linked `references/` with explicit load
  conditions. Keep deterministic repeated logic in tested, non-interactive `scripts/` with actionable errors.
- Give each Skill a concrete outcome, required inputs, workflow, failure
  path, and completion checks. Document script dependencies, inputs, working
  directory, `--help`, exit codes, limits, and side effects. Use dry runs,
  idempotency, or rollback when writes warrant them.
- Before adopting or updating a third-party Skill, inspect the pinned bundle,
  including dynamic commands, scripts, hooks, dependencies, outbound access,
  and license. Verify the publisher and version. Marketplace rank or a scan
  flag does not prove safety. Installation needs separate scope and authority.
- Consolidate business-module knowledge under `business-logic/references/<module>/`. Keep one compact `business-logic`
  entrypoint with direct links and load conditions; module guides have no `SKILL.md` or discovery frontmatter.
  Keep shared engineering, build, and test workflows in their existing Skills.
- For a new or materially changed trigger, draft 8-10 realistic should-trigger queries and 8-10 near-miss
  should-not-trigger queries. Measure invocation only when the active client exposes observable Skill calls; otherwise
  perform a manual boundary review and report that no trigger rate was measured.
- Evaluate output quality separately from activation. Define a few observable outcome, process, style, and efficiency
  checks; when the harness supports isolated runs, compare fresh sessions against the previous Skill or a no-Skill
  baseline and inspect traces and artifacts. Do not infer improvement from shorter text or successful activation. If
  fresh runs are unavailable, perform a source-backed manual review and report that limitation.

### 5. Validate and report

- Run the available Agent Skills validator, such as
  `skills-ref validate <skill-dir>`. Otherwise check YAML delimiters,
  standard fields, folder/name equality, description length, referenced
  paths, and absence of unintended placeholders directly.
- Run markdown diagnostics, line-ending/trailing-whitespace checks, broken-reference checks, and scoped
  `git diff --check`. Validate every path listed in `.agents/source-index.md` when it changes. Compare entrypoint and
  description sizes before/after; re-read routing files to confirm bridges remain thin and references load only on
  demand.
- Validate from every affected nested Git root. Run code/build/tests only when behavior or executable content changed;
  for documentation-only work, state what was skipped and why.
- Report changed surfaces, verified sources, capability boundaries, validation results, and unresolved risks.
- For a full refresh, re-read the actual files against the coverage rows.
  Report covered, inapplicable, pending, and blocked requirements separately.
  Give a concrete reason for each pending runtime check and keep its ID for
  handoff. A successful static check does not prove client behavior.

### 6. Retain verified lessons

- Capture a reproducible failure, cause, and check before changing guidance.
  Fix repeatable failures in tests or tooling where possible. Put repeatable
  procedures in the owning Skill and broad invariants in `AGENTS.md`.
- Keep personal preferences and temporary context in client-local memory
  subject to user consent. Record team rules in version control. Revisit
  old prompt workarounds when the client, model, or dependency changes.

## Repository invariants

- Keep engineering conventions in `../engineering-guidelines/SKILL.md`; route to them instead of duplicating details.
- Resolve `<BUILD_DIR>` from the nearest VS Code CMake setting, clangd compile-command path, or existing configured tree;
  fall back to `build`. Put scratch and script/log output under `<BUILD_DIR>/_agent_tmp/...`.
- Keep the root repository and direct subprojects independently maintainable; never make one prompt depend on another
  repository.

## References

- Read [business routing checks](references/business-routing-checks.md) only when changing the business Skill's scope,
  module layout, or loading conditions.
- Read [compatibility sources](references/compatibility-sources.md) only when changing Skill formats, discovery paths,
  bridge behavior, client-specific configuration, MCP guidance, or cross-tool compatibility claims.
- Read [writing guidance](references/writing-guidance.md) for substantial
  prose revisions, writing rules, titles, navigation, or terminology.
  Review related bilingual copy where applicable.
- Read [upstream change-workflow methods](../change-workflow/references/upstream-methods.md) only when changing risk
  routing or OpenSpec/Superpowers integration policy.
