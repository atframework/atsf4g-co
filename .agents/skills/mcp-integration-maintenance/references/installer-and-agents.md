# Installer and Agent Config Maintenance

Load only for setup.js / agents.mjs work: install flow, backend selection,
mirrors, agent config writing, switching, and uninstall.

## Layout and ownership

- `project/integration/mcp/setup.js` — the only install entry: backend choice
  (tgrep XOR CodeGraph) → mirror choice → dependency preparation → agent config
  write. `--uninstall` removes managed entries. Flags: `--backend=`,
  `--agents=<ids>|all`, `--mirror=cn|official`, `--offline`, `--skip-prepare`,
  `--all-agents`, `--yes`, `--dry-run`, `--help`, `--list-agents`.
- `agents/` — the agent auto-configuration component (P9.2):
  `src/registry.mjs` (products vs. physical targets, evidence links),
  `src/entries.mjs` (per-format entry serialization), `src/formats/jsonDocument.mjs`
  (JSON/JSONC surgical edits via the vendored jsonc-parser),
  `src/formats/codexToml.mjs` and `tomlDocument.mjs` (TOML editing and shared lexical boundaries), `src/configPlan.mjs`
  (pure planning with injectable I/O; shared-file dedup, conflicts, ownership
  gating), `src/fileStore.mjs` (containment, concurrency guard, temp+rename,
  backups/journal, ownership record, restore), `src/writers.mjs` (batch
  plan/apply + single-agent compat API). Unit-tested in `agents/test/`.
- `agents/vendor/jsonc-parser/` — pinned `jsonc-parser@3.3.1` (MIT), see its
  `VENDOR.json` for integrity hashes. Loaded through the UMD build via
  `createRequire`; the package's ESM build has extensionless imports and cannot
  be loaded by Node ESM. Upgrade = re-download, verify integrity, refresh
  VENDOR.json + LICENSE.
- `common/src/prepare.mjs` — dependency preparation library (npm install/ci,
  tgrep clone+patch+cargo build, CodeGraph `npm pack`+verify+extract, mirror
  parameters passed per invocation; no global npm/cargo config is modified).
- `common/src/agents.mjs` — compatibility shim re-exporting `agents/src/writers.mjs`
  (server ids `atsf4g-tgrep`, `atsf4g-codegraph`; `AgentConfigError` kinds:
  `unreadable`, `invalid-json`, `duplicate-key`, `not-object`, `map-not-object`,
  `toml-marker`, `conflict`, `concurrent-modification`, `outside-repo`). The
  agents component never imports this shim.
- `agents/src/guidance/codegraph.mjs` — the conditional CodeGraph prompt template
  and pure marker patcher. CodeGraph setup adds a root `AGENTS.md` step to the
  same config plan/apply batch; Claude's existing import remains the bridge.
  The patcher recognizes standalone markers outside Markdown fences. Preserve
  UTF-8/UTF-8 BOM or BOM-marked UTF-16 LE/BE and original newlines/outside text;
  unsupported encoding or broken/duplicate markers aborts the batch. Equal
  decoded bodies after newline normalization must not write bytes, update mtime,
  or create backups/journal entries. The file store supports Buffer snapshots
  so concurrent checks, backups and rollback retain exact encoded bytes.

## Non-negotiable behaviors

- Dependency preparation failure (network, build, hash mismatch) must abort
  before any agent config file is read or written. The exit path prints the
  failure and never mentions partial config success.
- `--dry-run` is fully read-only: no prepare, no prepared-state write, no
  network, no config writes; planned actions print with a 拟 (planned) prefix,
  never as 已写入 (written). `--help` / `--list-agents` return before any scan,
  prompt, or download; unknown agent ids and empty `--backend/--agents/--mirror`
  values are rejected before any prompt or download.
- Damaged configs abort the whole batch with zero writes and keep the original
  bytes: JSON syntax errors, non-object roots, duplicate keys, non-object
  server maps, unterminated/overlapping Codex markers. `agentStates()` reports
  damage as an `error` field instead of treating the file as unconfigured.
- Commented JSONC (including trailing commas) is valid input: edits go through
  jsonc-parser `modify`/`applyEdits` and preserve comments, foreign entries,
  key order, BOM, and line endings. Delete properties and commas separately so
  adjacent comments survive; whitespace at edit sites may change. Do not enable
  the optional formatter on existing files: it reformats neighboring user entries.
  Plain JSON clients never gain comments from us. Same-backend updates preserve
  optional settings and extra arguments; primary same-name foreign entries abort.
- Codex `.codex/config.toml` is edited textually between
  `# BEGIN/END atsf4g-mcp` markers; legacy marker-less `[mcp_servers.atsf4g-*]`
  tables (quoted keys and sub-tables included) end at the next real table
  header — table-header-like lines inside multi-line strings are never
  boundaries. Markers embedded in strings are data; foreign tables inside a
  marked block abort. Inline/dotted managed definitions require manual migration
  to table form; the lexical scanner is not a complete TOML validator. Never
  rewrite the whole file. State discovery and editing share the same scanner.
- Config changes run two-phase through `runAgentConfigBatch`: every target is
  planned read-only first (problems abort with zero writes), only a fully
  successful plan is applied. Apply writes via temp-file+rename from the build
  directory (EXDEV fails before replacement), re-compares against the planned original bytes (concurrent edits
  abort), backs up pre-write bytes under `<BUILD_DIR>/_agent_tmp/mcp/
  agent-config-backups/`, and rolls back already-written files when a later
  file fails. Record each config mutation before ownership/journal writes, so a
  post-rename metadata error also rolls back that file. Try all restores in reverse
  order, reporting individual failures and the backup directory. Concurrent edits
  or deletions keep the backup and are not overwritten. Planning and restoration
  validate containment too; file symlinks require manual editing. Not a cross-file
  transaction; crash recovery on the next run is still unimplemented.
- Whole-file deletion on uninstall requires the ownership record
  (`<BUILD_DIR>/integration/mcp/state/agent-config-state.json`) proving this
  integration created the file, its recorded file identity still matching, AND the
  remaining content being an empty comment-free managed skeleton. Old records
  without identity and pre-existing empty objects are kept. Restore ownership as
  well as content during rollback. Atomic metadata/backup files request mode 0600;
  updates/restores keep the original POSIX mode, without claiming equivalent Windows ACLs.
- `agentStates()` is only scanned after dependencies are ready (uninstall never
  prepares, so it scans immediately); the pre-prepare backend suggestion reads
  `prepared-state.json`, never agent configs.
- Re-running setup is the supported way to switch backends, upgrade pinned
  artifacts, add agents, or uninstall; switching removes the other managed backend
  before writing the selected one. A damaged state scan must not silently become
  an empty default selection.
- Patch root guidance only when setup configures at least one CodeGraph agent;
  preparation still precedes all guidance reads/writes. Help/list/dry-run remain
  read-only. Switching to tgrep or uninstalling MCP retains the conditional
  guidance and indexes; prompt insertion never initializes or opens an index.
  Source claims come from the pinned package: `CODEGRAPH_DIR` accepts a plain
  root-level directory name; shell use requires a matching prepared CLI and idle
  index. Avoid unpinned npx, full dynamic-dispatch guarantees, and forced indexing
  when the tool is missing, busy or incomplete.
- Output in setup.js goes through `process.stdout.write` directly. Do not use a
  readline interface for output: its `write()` produces nothing when stdin is
  not a TTY (verified on Windows/Node 24; this broke the installer once). The
  readline interface is closed in a `finally`. Non-TTY input/output never opens
  interactive questions; missing options fail immediately. Aliases resolve before
  prompts/preparation, and delimiter-only `--agents=,` is invalid.

## Agent matrix (formats verified against each vendor's docs at 2026-09-20, re-verified live before the P9.3 implementation)

| Agent | File (repo-relative) | Shape |
| --- | --- | --- |
| Claude Code + pi + CodeBuddy CLI (shared file, one group) | `.mcp.json` (legacy root `mcp.json` fallback for CodeBuddy) | `mcpServers` |
| Codex CLI | `.codex/config.toml` | `[mcp_servers.<id>]` + `cwd` + repo-relative args |
| VS Code / Copilot | `.vscode/mcp.json` | `servers` + `${workspaceFolder}` relative args |
| Cursor | `.cursor/mcp.json` | `mcpServers` |
| Gemini CLI | `.gemini/settings.json` | `mcpServers` |
| OpenCode | `opencode.json(c)` (multiple existing candidates abort) | `mcp`, `type: "local"`, `command: [prog, ...args]`, `environment`, `enabled` |
| Kilo (id `kilo`, alias `kilocode`; migrates from `.kilocode/mcp.json`) | `.kilo/kilo.json(c)`; root `kilo.json(c)` entries migrate into this directory | OpenCode shape; root and directory layers may coexist |
| Roo Code (migrates from `.roo/mcp_settings.json`) | `.roo/mcp.json` | `mcpServers` |
| ZCode | `.zcode/config.json` | `mcp.servers` |
| oh-my-pi | `.omp/mcp.json` | `mcpServers` |
| Zed | `.zed/settings.json` (JSONC) | `context_servers`, flat `command` + `args` |
| Kimi Code | `.kimi-code/mcp.json` | `mcpServers` + `cwd` + repo-relative args |
| Qwen Code | `.qwen/settings.json` | `mcpServers` + `cwd` + repo-relative args |
| WorkBuddy | `.workbuddy/mcp.json` | `mcpServers` |
| MiMo Code | `mimocode.json(c)` / `.mimocode/mimocode.json(c)`, new default the latter | OpenCode shape |

Docs-only (user-level files, not auto-written): Windsurf
(`~/.codeium/windsurf/mcp_config.json`, variable interpolation in
command/args/env), Cline (VS Code panel or `~/.cline/data/settings/cline_mcp_settings.json`).
pi needs the third-party `pi-mcp-adapter` extension to read the project
`.mcp.json`; setup prints that prerequisite when pi is selected.

Shared-file and migration rules (Plan.md 11.3): selecting any member of the
`.mcp.json` group keeps the group (no removal is planned for unselected
members while another member is selected); uninstalling any member affects
the whole group and the summary labels the consumers. Exclusive candidates
(OpenCode root pair, Kilo `.kilo/` pair, MiMo root/directory candidates) abort when
several exist as a conservative installer limitation, not a claim that clients
cannot merge files. Kilo root configs may coexist with the `.kilo/` target.
Legacy cleanup (`.kilocode/mcp.json`, `.roo/mcp_settings.json`, legacy
`mcp.json`) runs only after the new target was written and only claims
entries whose command/args verifiably point at this repository's wrappers.
Creating `.mcp.json` over a foreign-populated legacy `mcp.json` is refused
(shadowing) unless CodeBuddy is the sole selection, in which case the legacy
file is edited in place. Remove-only operations edit the sole legacy root file
in place and preserve foreign servers. Missing legacy files are normal. Legacy
descriptors can specify a format (Kilo root configs use `mcp`, not `mcpServers`).
Migration with custom options/extra arguments aborts rather than discarding them;
manually migrate those settings and remove the old managed entry before retrying.
Explicit uninstall can remove a verified legacy entry with custom settings.

When adding an agent or doubting a format, re-check the vendor's current docs
first and update this table, `agents/src/registry.mjs`, and the README examples
together.

## Path policy

VS Code entries use `${workspaceFolder}`. Codex, Kimi and Qwen use absolute `cwd`
+ repo-relative args, so those are still machine-local configs. Cursor supports
`${workspaceFolder}` interpolation and Gemini supports `cwd`; this implementation
currently uses absolute args for them and the other JSON clients. Do not infer a
client lacks cwd/variable support from the serializer's current choice.

## Validation checklist

1. Each component's `npm test` (or `node --test` with explicit `test/*.test.mjs`
   file lists — directory arguments fail as module paths) in `agents`, `common`,
   `tgrep`, `codegraph`. `agents/package.json` must include new regression files;
   setup tests execute an isolated CLI install/repeat/switch/uninstall flow.
2. `node project/integration/mcp/setup.js --dry-run ...` variants: install with
   `--agents=all`, switch backends twice, `--uninstall --all-agents`; confirm
   actions are listed with the 拟 prefix and no files changed
   (`git status --porcelain` before/after). Also spot-check `--help`,
   `--list-agents`, and an unknown `--agents` id (must fail before any prompt).
3. Real run in a scratch clone or after backing up configs; verify foreign
   entries survive (including in commented JSONC files). Run `doctor.mjs` without
   stopping active clients; an existing live lock is not a successful idle-state
   acceptance and must be reported as such.
4. Manual trigger boundary review for the Skill description when its wording
   changes (no invocation telemetry exists for this client).
