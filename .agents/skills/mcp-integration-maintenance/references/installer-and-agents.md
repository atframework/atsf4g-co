# Installer and Agent Config Maintenance

Load only for setup.js / agents.mjs work: install flow, backend selection,
mirrors, agent config writing, switching, and uninstall.

## Layout and ownership

- `project/integration/mcp/setup.js` — the only install entry: backend choice
  (tgrep XOR CodeGraph) → mirror choice → dependency preparation → agent config
  write. `--uninstall` removes managed entries. Flags: `--backend=`,
  `--agents=<ids>|all` (all = auto-writable targets only; guided imports stay
  opt-in), `--mirror=cn|official`, `--offline`, `--skip-prepare`,
  `--all-agents`, `--yes`, `--ui=line` (numbered-line menus instead of arrow
  keys), `--dry-run`, `--help`, `--list-agents`.
- `agents/` — the agent auto-configuration component (P9.2; per-agent
  configurators since 2026-09-21, Plan.md 11.16):
  `src/agents/` holds ONE module per product (`claude.mjs` … `codebuddy-ide.mjs`,
  20 total) exporting a configurator built from shared base classes in
  `src/agents/base.mjs` (`AgentConfigurator`, `JsonServerMapConfigurator`,
  `OpenCodeShapeConfigurator`, `CodexTomlConfigurator`,
  `GuidedImportConfigurator`); the shared `.mcp.json` group target is declared
  once in `src/agents/mcp-json-group.mjs` and `src/agents/index.mjs`
  aggregates/validates the REGISTRY/TARGETS surfaces. `src/registry.mjs` is a
  compatibility re-export (also of `src/backends.mjs`); adding an agent = one
  new module + one index registration. Keep `installNotes` and IDE `importSpec`
  in those product modules too; do not add product switches to setup.js.
  Engine modules stay product-agnostic:
  `src/entries.mjs` (per-format entry serialization), `src/formats/jsonDocument.mjs`
  (JSON/JSONC surgical edits via the vendored jsonc-parser),
  `src/formats/codexToml.mjs` and `tomlDocument.mjs` (TOML editing and shared lexical boundaries), `src/configPlan.mjs`
  (pure planning with injectable I/O; shared-file dedup, conflicts, candidate
  consolidation, ownership gating), `src/migration/` (recursive JSON merge and
  verified legacy option translation), `src/fileStore.mjs` (containment,
  concurrency guard, temp+rename, backups/journal, ownership record, restore),
  `src/writers.mjs` (batch plan/apply + single-agent compat API). Unit-tested
  in `agents/test/` (consolidation coverage in `consolidation.test.mjs`).
- `agents/src/ui/terminalMenu.mjs` — the installer menus: arrow-key selection
  (raw mode keypress events) with a numbered-line fallback; `MenuCancelled`
  restores the terminal on Escape/Ctrl+C/EOF/render errors; empty menus fail
  immediately and a closed stdin never confirms a default.
- `agents/src/guidance/ideExports.mjs` — guided-import snippets for IDE clients
  without a verified project file (Cline IDE, CodeBuddy IDE): derived JSON in
  `<BUILD_DIR>/integration/mcp/exports/`, written on apply, removed on
  uninstall or deselection. Plan these in the same batch as configs and root
  guidance; never write them after the batch commits. Discover existing snippets
  for default selections. Reject modified snippets and escaped paths before any
  write; rollback and concurrent-edit protection use the existing file store.
  Removing a snippet does not remove entries manually imported into an IDE.
- `agents/tools/launch.mjs` — the explicit Cline launcher: sets
  `CLINE_MCP_SETTINGS_PATH` to the repo export file and the repo cwd for the
  child only, spawns a verified entry with `shell:false` (resolution:
  `--cline` > `CLINE_BIN_PATH` > npm global `cline/bin/cline` script >
  platform native binary; `.cmd`/`.bat` shims are rejected on every platform —
  a Windows-style path passed from WSL fails the same way). Explicit and env
  overrides share file validation; Node shebang scripts (including extensionless
  `bin/cline`) run under Node. Invalid overrides fail instead of falling back.
  Consume `CLINE_BIN_PATH` in the launcher and omit it from the child environment:
  the pinned npm resolver reads it again and can bypass the selected entry or recurse.
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
  interactive UI (arrows or line) is created only when `--yes` is unset, `CI`
  is absent, and both stdin/stdout are TTYs; non-interactive input/output never
  opens interactive questions; validate missing choices before dependency
  preparation. Keep `--yes` defaults for the prepared backend and existing selections.
  Aliases resolve
  before prompts/preparation, and delimiter-only `--agents=,` is invalid.
- An empty agent selection never silently uninstalls: interactive runs list the
  configured entries that would be removed and require an explicit y/N (empty
  input declines); non-interactive runs error out and point at `--uninstall`.
  `--agents=all` covers only products with a repo target; extension/launcher/
  panel-import clients are summarized as pending user actions.
- Menu cleanup must settle pending questions on stream end/close/error and UI
  close, restore raw mode, and pause stdin so a completed installer can exit.
  Handle readline's own close/SIGINT too (Ctrl+D/Ctrl+C need not close stdin).
  Test simulated TTYs with an explicit TERM and clean up test deadline timers;
  keep TERM=dumb fallback coverage. Use real PTY subprocess exit as evidence,
  since a final output marker alone does not prove handle cleanup. Cancellation
  after preparation preserves downloaded dependencies; do not call it zero writes.

## Agent matrix (formats verified against each vendor's docs at 2026-09-20, re-verified live before the P9.3 implementation)

| Agent | File (repo-relative) | Shape |
| --- | --- | --- |
| Claude Code + pi + CodeBuddy CLI (shared file, one group) | `.mcp.json` (legacy root `mcp.json` fallback for CodeBuddy) | `mcpServers` |
| Codex | `.codex/config.toml` | `[mcp_servers.<id>]` + `cwd` + repo-relative args; shared by CLI and IDE |
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
| Cline CLI | `.cline/atsf4g-mcp.json` (installer-defined export; Cline does not discover it) | `mcpServers` + explicit launcher |
| Cline IDE / CodeBuddy IDE (`cline-ide`, `codebuddy-ide`) | none (guided import) | build-dir snippet + panel steps; excluded from `--agents=all` |

Use product names for verified shared CLI/IDE configuration targets; keep
existing ids stable. `Codex` shares host configuration between CLI and IDE.
`Kimi Code` names the product, but `.kimi-code/mcp.json` applies to the current
runtime; the official VS Code extension still has legacy/new-runtime compatibility
limits. See the README's official links. Do not turn a CLI acceptance record or
a display-name change into a claim that every IDE version is supported.

The cline row is verified against the released `cline@3.0.62` binary: MCP
settings come from `CLINE_MCP_SETTINGS_PATH` or the user-level
`~/.cline/data/settings/cline_mcp_settings.json`; the project `.cline/mcp.json`
line in the official CLI reference is a known docs error (cline/cline#11671).
Never claim cline was auto-connected — the export needs
`agents/tools/launch.mjs`.

Docs-only (user-level files, not auto-written): Windsurf
(`~/.codeium/windsurf/mcp_config.json`, variable interpolation in
command/args/env). pi needs the third-party `pi-mcp-adapter` extension to read
the project `.mcp.json`; setup prints that prerequisite when pi is selected and
also reports same-name conflicts (or unparsable content) in a higher-priority
`.pi/mcp.json` without ever writing that file.

Shared-file and migration rules (Plan.md 11.3/11.17): selecting any member of
the `.mcp.json` group keeps the group (no removal is planned for unselected
members while another member is selected); uninstalling any member affects the
whole group and the summary labels the consumers. OpenCode, Kilo and MiMo
consolidate JSON/JSONC candidates only within the same directory, into its
first existing candidate. Recursively merge compatible object fields; differing
leaves/arrays (including managed options) abort with `consolidation-conflict`.
Preserve destination BOM/EOL and edit existing documents surgically. Move source
comments into a labeled block at the destination's end. Recompute the final
action against ORIGINAL destination bytes: an unchanged intermediate edit must
never skip the merged write and then delete its source. Before every source
cleanup, verify that the destination still contains the planned result, even
when its action was unchanged. Delete redundant files after writing the
destination, with backups, journal and batch rollback.
Root and hidden-directory configs remain separate upstream layers; foreign
settings/plugins never relocate across directories. MiMo uses an existing
`.mimocode/` layer when available, otherwise edits the existing root layer.
Removal-only batches clean each existing candidate in place without merging.
Legacy cleanup (`.kilocode/mcp.json`, `.roo/mcp_settings.json`, legacy
`mcp.json`) runs only after the new target was written and only claims
entries whose command/args verifiably point at this repository's wrappers.
Creating `.mcp.json` over a foreign-populated legacy `mcp.json` is refused
(shadowing) unless CodeBuddy is the sole selection, in which case the legacy
file is edited in place. Remove-only operations edit the sole legacy root file
in place and preserve foreign servers. Missing legacy files are normal. Legacy
descriptors can specify a format (Kilo root configs use `mcp`, not `mcpServers`).
Same-format migration preserves custom options and extra arguments. Legacy Kilo
stdio maps `env` to `environment` and `disabled` to inverted `enabled`.
Unknown cross-format fields, custom cwd, `{file:...}` substitutions, conflicting
values and custom options belonging to a different selected backend abort with
zero writes; do not guess translations or silently discard options.
Explicit uninstall can remove a verified legacy entry with custom settings.

When adding an agent or doubting a format, add or edit its module under
`agents/src/agents/`, register it in `agents/src/agents/index.mjs`, re-check
the vendor's current docs, and update this table plus the README examples
together. `--list-agents` output vs. the registry and the `--help` option
surface are guarded by a dedicated regression in `agents/test/setup.test.mjs`.

## Client acceptance notes (2026-09-20, Plan.md 11.14)

Verified against released clients in an isolated repo + isolated HOME (both
configs and connection semantics):

- qwen-code 0.24.1: project entry discovered; `qwen mcp approve atsf4g-tgrep`
  is the user step; afterwards `qwen mcp list` reports **Connected** (live
  initialize handshake).
- cline 3.0.62: `cline config` under a real TTY (launcher + ConPTY) shows the
  export file loaded on the MCP tab; `cline doctor` exits 0 through the
  launcher. In-session tool calls need a model provider login (not verified).
- codex 0.155.1: project `.codex/config.toml` is ignored until the user enables
  project config and trusts the project (user-level
  `project_config_enabled = true` plus `[projects.'<abs path>']
  trust_level = "trusted"`); then `codex mcp list`/`get` resolve the entry.
- gemini 0.60.0: project entry discovered but disabled in untrusted folders
  (official behavior); the trust bypass path requires model auth.
- claude code 2.1.278: `mcp list` discovers `.mcp.json` entries and reports
  ⏸ Pending approval; unapproved servers are never connected, approval is an
  interactive step inside `claude`, and hand-writing
  `enabledMcpjsonServers`/project settings did not bypass it. The npmmirror
  `@anthropic-ai/claude-code` `bin/claude.exe` is a broken ~500-byte stub; run
  the platform package `claude-code-<plat>-<arch>/claude.exe` directly.

Environment caveats for future acceptance rounds: `TGREP_EXCLUDE_DIRS` matches
directory NAMES (`build`, `build_jobs_cmake_tools`, ...), so a scratch repo
nested under the build directory indexes zero files — place real-backend
scratch projects outside those names (e.g. the OS temp dir). Windows ConPTY
drives fine via `node-pty` (its exit helper prints a harmless
`AttachConsole failed`); `winpty` cannot run under piped harness shells.

## Lockfile policy

`common/tgrep/codegraph/package-lock.json` must stay mirror-neutral: no
`resolved` entries, or ones pointing at `registry.npmjs.org`/`
`registry.npmmirror.com` only. npm 12 fails `npm ci` with `EALLOWREMOTE` on
third-party tarball hosts (this broke fresh installs until 2026-09-20 — the
lockfiles carried a Tencent mirror host). `common/test/lockfiles.test.mjs`
guards it; after regenerating a lockfile against any registry, strip foreign
`resolved` hosts before committing.

## Path policy

VS Code entries use `${workspaceFolder}`. Codex, Kimi and Qwen use absolute `cwd`
+ repo-relative args, so those are still machine-local configs. Cursor supports
`${workspaceFolder}` interpolation and Gemini supports `cwd`; this implementation
currently uses absolute args for them and the other JSON clients. Do not infer a
client lacks cwd/variable support from the serializer's current choice.

## Validation checklist

1. Each component's `npm test` (or `node --test` with explicit `test/*.test.mjs`
   file lists — directory arguments fail as module paths) in `agents`, `common`,
   `tgrep`, `codegraph`, on Windows AND WSL/Linux. `agents/package.json` and
   `common/package.json` must include new regression files;
   setup tests execute an isolated CLI install/repeat/switch/uninstall flow.
2. `node project/integration/mcp/setup.js --dry-run ...` variants: install with
   `--agents=all`, switch backends twice, `--uninstall --all-agents`; confirm
   actions are listed with the 拟 prefix and no files changed
   (`git status --porcelain` before/after). Also spot-check `--help`,
   `--list-agents`, and an unknown `--agents` id (must fail before any prompt).
3. Full-cycle acceptance (when the agent-config code changes): run
   `node <BUILD_DIR>/_agent_tmp/mcp/run-cli-cycle.mjs` on Windows and WSL —
   install → idempotent rerun → double switch → uninstall ×2 → dry-runs →
   corrupted-last-target abort, with byte-level repo+HOME snapshots and a dead
   proxy proving no downloads. Add a multi-candidate fixture (e.g. Kilo
   `kilo.json` + `kilo.jsonc`) whenever candidate handling changes and verify
   consolidation merges, deletes after the write, and rolls both back on a
   later injected failure. Real-backend scratch projects go in the OS temp
   dir, never under the build directory (see Client acceptance notes).
4. `git check-ignore --no-index -v` sweep over every target/candidate/legacy
   path enumerated from `agents/src/agents/index.mjs` after changing targets
   or `.gitignore`; also confirm none of them is tracked and that
   `project/integration/mcp/` + Skill sources stay committable.
5. Real run in a scratch clone or after backing up configs; verify foreign
   entries survive (including in commented JSONC files). Run `doctor.mjs` without
   stopping active clients; an existing live lock is not a successful idle-state
   acceptance and must be reported as such.
6. Manual trigger boundary review for the Skill description when its wording
   changes (no invocation telemetry exists for this client).
