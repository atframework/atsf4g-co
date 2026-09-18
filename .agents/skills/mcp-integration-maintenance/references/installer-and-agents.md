# Installer and Agent Config Maintenance

Load only for setup.js / agents.mjs work: install flow, backend selection,
mirrors, agent config writing, switching, and uninstall.

## Layout and ownership

- `project/integration/mcp/setup.js` — the only install entry: backend choice
  (tgrep XOR CodeGraph) → mirror choice → dependency preparation → agent config
  write. `--uninstall` removes managed entries. Flags: `--backend=`,
  `--agents=<ids>|all`, `--mirror=cn|official`, `--offline`, `--skip-prepare`,
  `--all-agents`, `--yes`, `--dry-run`.
- `common/src/prepare.mjs` — dependency preparation library (npm install/ci,
  tgrep clone+patch+cargo build, CodeGraph `npm pack`+verify+extract, mirror
  parameters passed per invocation; no global npm/cargo config is modified).
- `common/src/agents.mjs` — agent config writers; unit-tested in
  `common/test/agents.test.mjs`. Server ids: `atsf4g-tgrep`, `atsf4g-codegraph`.

## Non-negotiable behaviors

- Dependency preparation failure (network, build, hash mismatch) must abort
  before any agent config file is read or written. The exit path prints the
  failure and never mentions partial config success.
- Managed entries are merged into project-level files inside the repository;
  foreign entries, comments, and keys survive every add/switch/remove. A file
  that ends up as a skeleton we created is deleted on uninstall.
- Codex `.codex/config.toml` is edited textually between
  `# BEGIN/END atsf4g-mcp` markers (legacy marker-less tables are also
  recognized); never rewrite the whole file.
- Re-running setup is the supported way to switch backends, upgrade pinned
  artifacts, add agents, or uninstall; switching removes both managed server
  ids before writing the new one.
- Output in setup.js goes through `process.stdout.write` directly. Do not use a
  readline interface for output: its `write()` produces nothing when stdin is
  not a TTY (verified on Windows/Node 24; this broke the installer once).

## Agent matrix (formats verified against each vendor's docs at 2026-09)

| Agent | File (repo-relative) | Shape |
| --- | --- | --- |
| Claude Code + pi | `.mcp.json` | `mcpServers` |
| Codex CLI | `.codex/config.toml` | `[mcp_servers.<id>]` + `cwd` + repo-relative args |
| VS Code / Copilot | `.vscode/mcp.json` | `servers` + `${workspaceFolder}` relative args |
| Cursor | `.cursor/mcp.json` | `mcpServers` |
| Gemini CLI | `.gemini/settings.json` | `mcpServers` |
| OpenCode | `opencode.json` | `mcp`, `type: "local"`, `command: [prog, ...args]`, `environment`, `enabled` |
| Kilo Code | `.kilocode/mcp.json` | `mcpServers` |
| Roo Code | `.roo/mcp_settings.json` | `mcpServers` |
| ZCode | `.zcode/config.json` | `mcp.servers` |
| oh-my-pi | `.omp/mcp.json` | `mcpServers` |

Docs-only (user-level files, not auto-written): Windsurf
(`~/.codeium/windsurf/mcp_config.json`, variable interpolation in
command/args/env), Cline (VS Code panel or `~/.cline/data/settings/cline_mcp_settings.json`).

When adding an agent or doubting a format, re-check the vendor's current docs
first and update this table, `agents.mjs`, and the README examples together.

## Path policy

VS Code entries use `${workspaceFolder}` and Codex uses `cwd` + repo-relative
args (both officially supported, commit-friendly). All other agents receive
absolute paths resolved at setup time because their clients have no documented
cwd/variable contract. Do not switch an agent to relative paths based on
anecdotes.

## Validation checklist

1. `node --test` in `common` (agents tests included), then `tgrep`, `codegraph`.
2. `node project/integration/mcp/setup.js --dry-run ...` variants: install with
   `--agents=all`, switch backends twice, `--uninstall --all-agents`; confirm
   actions listed and no files changed.
3. Real run in a scratch clone or after backing up configs; verify foreign
   entries survive and `doctor.mjs` stays green.
4. Manual trigger boundary review for the Skill description when its wording
   changes (no invocation telemetry exists for this client).
