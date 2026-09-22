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
2. Read the current state before editing: `project/integration/mcp/README.md`
   (user-facing install/config surface and acceptance records) plus the two
   reference pages below (design contracts and verified upstream facts).
   Check the dirty tree, then run
   `node project/integration/mcp/common/tools/doctor.mjs` for live state.
3. Route to one reference, not both:
   - Installer, agent selection/uninstall, mirror behavior, or per-agent config
     formats → read `references/installer-and-agents.md`. Keep product-specific
     targets, prerequisites, import steps, and launch notes in that product's
     `agents/src/agents/` module; reuse the shared format and migration engines.
   - Wrapper servers, the tgrep stdio patch, backend spawn/lifecycle, index
     reuse, or upstream upgrades → read `references/backends-and-lifecycle.md`.
4. Preserve the load-bearing contracts (details in the reference pages): pinned upstream
   commits/versions with dependency integrity checks; prefer verified local tools,
   otherwise prepare CodeGraph through pinned npx with an offline/library probe;
   dependency preparation must fully succeed before any agent config is touched;
   `--dry-run` and `--help`/`--list-agents`/`--list-mirrors`
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
  `references/backends-and-lifecycle.md` against the new sources.
- Keep `README.md` in sync with implemented behavior; use `<PROJECT_DIR>` for
  workspace paths and `<MCP_DIR>` for the movable toolkit directory. Do not embed
  this repository's name, checkout path or toolkit layout in the toolkit itself.
- `setup.js`, doctor and the Cline launcher discover the nearest project above
  cwd, falling back to cwd; explicit `--repo-root` wins where supported. The
  toolkit resolves its own location from `import.meta.url`. Propagate both
  locations through planning, exports and preparation. Every new client entry
  pins `--repo-root`, including local-toolkit entries. Legacy wrapper launches
  without that argument detect the project enclosing the toolkit, then fall back
  to cwd discovery. No installer marker or fixed project path is required.
- Project names, build/cache defaults and CodeGraph directory names come from
  local metadata. Client keys remain `workspace-tgrep` / `workspace-codegraph`.
  Migrate old prefixes only after verifying the backend, wrapper path and scope;
  preserve options/comments, and abort on old/new key collisions.
