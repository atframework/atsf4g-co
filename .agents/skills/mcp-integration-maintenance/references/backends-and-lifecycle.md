# Backend Wrapper and Lifecycle Maintenance

Load only for wrapper-server, backend, patch, index, or upstream-upgrade work
under `project/integration/mcp/tools/{tgrep,codegraph,sirchmunk}` and `common/`.

## Verified facts you may rely on

Each fact below was verified against the pinned upstream sources/artifacts
(see `tools/tgrep/upstream-lock.json` and `tools/codegraph/upstream-lock.json`; the README
"已验证平台" table records when). Re-verify against the new pinned sources
before relying on them after an upgrade.

- `common/src/sharedService.mjs` owns one backend per user/workspace/tool/cache.
  Each Agent keeps a stdio frontend; authenticated local IPC connections own the
  shared lifetime. Kernel-exclusive Windows named pipes/Linux abstract sockets
  elect the worker before backend startup. A frontend exit releases its own
  connection; the last disconnect stops the backend before releasing IPC.
  Losing candidate workers never open the index. Do not add TCP, steal live
  ownership, or replay in-flight requests after a worker crash. Configuration
  mismatch fails explicitly; diagnostic writes cannot block cleanup.
  Workers use installed Node 20.8+; Deno uses an EOF-bound Node relay without
  widening its permissions. Other POSIX socket crash recovery is unverified
  and fails closed instead of unlinking an endpoint of unknown ownership.
- Both backends exit on stdin EOF; CodeGraph direct mode additionally has a
  PPID watchdog and a liveness-watchdog child that exits on its own stdin
  lifeline. This is why the wrappers need no Windows Job Object or POSIX
  guardian: normal stop is EOF → SIGTERM → SIGKILL against only our child pid
  (the SDK `StdioClientTransport.close()` implements this order for CodeGraph;
  `common/src/supervisor.mjs` does it for tgrep).
- CodeGraph's library needs the built-in `node:sqlite` module (Node 22.5+ even
  though its package.json says >=20). Both the serve child and the first-index
  helper (`tools/codegraph/src/initialize.mjs`) must run on the same verified Node
  with `--liftoff-only --disable-warning=ExperimentalWarning`. Platform packages
  use their own Node; compiled source checkouts use a probed system Node.
  The POSIX `bin/codegraph` shell wrapper is never spawned (it re-injects
  `CODEGRAPH_HOST_PPID`).
- CodeGraph `serve --mcp` never creates the first index; the wrapper runs the
  library init helper when `<repo>/.codegraph-<project>-<platform>/codegraph.db`
  (or a valid `.codegraph/`) is missing. Tool schemas are taken from the
  backend's `tools/list` with `projectPath` stripped; the pinned static schemas
  are only the pre-ready fallback.
- The direct-mode MCP engine owns file watching and reconnect catch-up. Do not
  start a second writer or detached daemon. Report watcher diagnostics through
  status.auto_sync rather than inferring successful watching from a handshake.
  Initialization obeys abort and stdin EOF too; closing during first indexing
  must stop its helper before releasing the wrapper lock.
- CodeGraph 1.6.0 hides `codegraph_status` from `tools/list` for indexes with
  fewer than 500 files, even with an explicit tool allowlist. The wrapper
  probes that read-only handler before using its pinned schema; a missing
  handler still fails readiness. Keep both cases in protocol tests and run a
  real small-workspace smoke test.
- tgrep speaks newline JSON-RPC (`search`/`files`/`status`; `reload` never
  exposed) through the integration patch adding `--transport stdio`. The patch
  keeps `serve.lock` exclusivity and adds a backend-side 20 000-row cap.
  Revision `mcp-stdio-v2` also fixes inherited ignore matching above an explicit
  served root: the walk enters that root, so watcher parent checks must stop
  there while preserving rules for descendants. Patch 0002 covers `.gitignore`,
  `.ignore` and repository excludes; preserve its CR bytes (pinned core source
  uses CRLF). Prepare verifies every patch and rebuilds incompatible output;
  runtime startup rejects older binaries. Do not infer freshness from
  watcher_active: run `tools/tgrep/tools/native-watcher-smoke.mjs <binary>
  <build-scratch-dir>` for real add/edit/rename/delete, reconnect and warm edits.

## Upgrade flow (pinned upstream)

1. Update the tool's `upstream-lock.json` (commit, version, shasums). For a new
   tgrep pin, delete `<PROJECT_DIR>/.mcp-data/downloads/sources/tgrep-src` so prepare
   re-clones; re-apply the patch (regenerate it if it no longer fits) and record
   the new patch sha.
2. Re-verify the facts listed above against the new sources before changing
   wrapper code (especially stdin-EOF behavior, `CODEGRAPH_DIR` semantics, tool
   schemas, and the internal RPC shapes). The pinned npm artifacts are the
   authority when they differ from the inspected source tree (the tree has
   been ahead of the release before: 1.6.0 artifacts lack the `ui` subcommand
   the source has) — check the actual artifact's `--help`, not only the repo.
3. Run `node project/integration/mcp/setup.js` (or the prepare library) and the
   five test suites; then a real-backend smoke per README platform table.
4. Update the README verified-platform table with what was actually measured.

## Index and state rules

- Sirchmunk 0.2.0: the official MCP layer does not forward
  `enable_knowledge_evolution`. The private Python bridge uses the pinned SDK,
  starts without knowledge reuse, then attaches its embedding client and
  `KnowledgeEvolver` between serialized requests after `EmbeddingUtil.is_ready()`.
  Failures keep basic search available. Model snapshots pin a Hugging Face commit;
  offline mode must not fall back to network. Native dependencies are prepared
  before config writes; never invoke the upstream global rg/rga auto-installer.
  Its MCP example's `list_all()` is absent from the wheel: use bounded `find('', limit)`.
  Flush completed search storage before returning, and close storage on graceful shutdown.
  On Windows, blocking stdin reads stall NumPy initialization; peek the pipe and
  read only available bytes while monitoring EOF, including during native imports.
  `bridge_test.py` covers state transitions, scoped paths, OS download locks,
  EOF cleanup and Windows Job Object cleanup on forced exit. Its installer-only
  model worker intentionally survives setup and exits after preparation.

- tgrep index: `<PROJECT_DIR>/.mcp-data/state/tgrep/<workspace-id>/<platform>/index`
  with one workspace-selected exclude list (UE layouts omit generated/resource
  directories but keep source Build directories); policy changes must stay identical across
  index/serve/watch or the index can drop members.
- Explicit wrapper `--repo-root` can refer to a workspace outside the toolkit;
  every new client config pins this argument. Legacy launches detect the project
  enclosing the toolkit, falling back to cwd discovery if none exists. Never
  reintroduce a fixed installer path or project name. Prepare resolves toolkit
  files separately from workspace state. UE CodeGraph setup plans `codegraph.json` includes/excludes in the same
  guarded batch as client configs; pinned 1.6.0 `project-config.js` confirms that
  explicit excludes override includes, including in Perforce-only source trees.
- CodeGraph index: repository-root `.codegraph-<project>-<platform>`, where
  project is normalized from metadata by `projectInfo`. Reuse an existing
  `.codegraph/codegraph.db` first, then exactly one `.codegraph-*-<platform>`
  database, including an old project prefix. Multiple platform candidates abort;
  do not pick one silently or create a replacement. Windows and WSL keep separate
  directories. Ensure project ignore rules cover generated indexes. Manual CLI/SQLite access
  (including the `CODEGRAPH_DIR` incantation and the no-concurrent-writers
  rule) is documented in the README section "手动查询或操作 CodeGraph 索引" —
  keep that section accurate when index handling changes.
- Shared service status: `shared-service.json` reports PID and client count;
  authentication material belongs only in the ignored `private/` directory.
  Status writes are best effort and never a prerequisite for shutdown.
- Wrapper state/locks: `wrapper-state.json` (atomic replace) and the O_EXCL
  instance lock with pid-liveness steal; backends' own locks (`serve.lock`,
  `writer.pid`) are the final exclusivity. Never kill by process name or stale
  pid file.

## Validation checklist

1. Unit tests in all five packages; protocol tests spawn the real server
   entry with a fake backend — keep them free of real-index dependencies.
2. Real-backend smoke (scratch script pattern under
   `<PROJECT_DIR>/.mcp-data/tmp/smoke.mjs`): first index, reuse start, one real
   query, client close → server and backend exit, no leftover processes, locks
   released. Check add/edit/delete refresh and offline edits caught up after
   reconnect. Dependency changes also require a real npx cache warm-up plus
   offline execution/library verification, and existing-local-tool reuse.
3. For lifecycle changes, test simultaneous Agents, one frontend closing while
   others query, last-connection cleanup, and forced shared-worker exit. Check
   actual backend process counts and no replay of interrupted calls. Keep
   `common/test/shared-service.test.mjs` in the common suite.

Workspace data is fixed at `<PROJECT_DIR>/.mcp-data/`; use `tmp/` for verification logs and fixtures.
`--build-dir` only locates legacy read sources. Build settings never select another shared service.
Normalize Windows workspace identity case, preserve live legacy locks, and keep IPC mandatory for
query forwarding and lifecycle. A live PID with unreachable IPC suppresses new candidates; it never
grants ownership. Reap owned election losers after authenticated attachment to avoid late idle owners.
