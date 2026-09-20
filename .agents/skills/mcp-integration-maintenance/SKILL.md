---
name: mcp-integration-maintenance
description: "Use only when implementing, upgrading, configuring, debugging, or testing the tgrep/CodeGraph MCP wrappers, the setup.js installer, their agent config writers, or their indexes under project/integration/mcp. Do not use for ordinary code search, calling these MCP tools, C++ work, or unrelated MCP services."
---

# MCP Integration Maintenance

Maintain the two stdio MCP wrappers (tgrep text search, CodeGraph structural
navigation), their installer, and their agent integrations under
`project/integration/mcp`. Users who only run `node project/integration/mcp/setup.js`
or call the MCP tools do not need this Skill.

## Steps

1. Confirm the task is maintenance of this integration (install/upgrade/uninstall
   behavior, wrapper code, the tgrep patch, agent config writing, index/lifecycle
   issues). Otherwise stop; ordinary repository work does not load this Skill.
2. Read the current state before editing: `project/integration/mcp/Plan.md`
   (design contracts, verified upstream facts, phase status) and
   `project/integration/mcp/README.md` (user-facing install/config surface).
   Check the dirty tree, then run
   `node project/integration/mcp/common/tools/doctor.mjs` for live state.
3. Route to one reference, not both:
   - Installer, agent selection/uninstall, mirror behavior, or per-agent config
     formats → read `references/installer-and-agents.md`.
   - Wrapper servers, the tgrep stdio patch, backend spawn/lifecycle, index
     reuse, or upstream upgrades → read `references/backends-and-lifecycle.md`.
4. Preserve the load-bearing contracts (details in Plan.md): pinned upstream
   commits/versions with hash checks; dependency preparation must fully succeed
   before any agent config is touched; `--dry-run` and `--help`/`--list-agents`
   are side-effect-free; damaged config files abort the whole batch with zero
   writes; agent configs are project-level files in the repository and foreign
   entries must survive; backends run with no listening port and no telemetry,
   and stop via the stdin lifeline (EOF → SIGTERM → SIGKILL).
5. Validate what changed: run the four component test suites (each `npm test`,
   or `node --test` with explicit `test/*.test.mjs` file lists — directory
   arguments fail) for `agents`, `common`, `tgrep`, `codegraph` under
   `project/integration/mcp`; for installer changes also smoke the CLI
   (`--dry-run` install/uninstall variants with a `git status --porcelain`
   before/after); for lifecycle, spawn, or index changes also run a
   real-backend smoke and check for leftover processes/locks. Windows x64 and
   Linux x64 (WSL) are verified platforms; report anything else as untested.

## Boundaries

- Do not edit agent config files by hand when `setup.js` can produce them; fix
  the installer instead.
- Do not upgrade pinned upstream versions without re-verifying the facts in
  Plan.md section 2 against the new sources.
- Keep `Plan.md` and `README.md` in sync with implemented behavior; use the
  `<PROJECT_DIR>` placeholder for repository paths in docs and examples.
