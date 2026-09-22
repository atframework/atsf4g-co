# Backend Wrapper and Lifecycle Maintenance

Load only for wrapper-server, backend, patch, index, or upstream-upgrade work
under `project/integration/mcp/{tgrep,codegraph,common}`.

## Verified facts you may rely on

Each fact below was verified against the pinned upstream sources/artifacts
(see `tgrep/upstream-lock.json` and `codegraph/upstream-lock.json`; the README
"已验证平台" table records when). Re-verify against the new pinned sources
before relying on them after an upgrade.

- Both backends exit on stdin EOF; CodeGraph direct mode additionally has a
  PPID watchdog and a liveness-watchdog child that exits on its own stdin
  lifeline. This is why the wrappers need no Windows Job Object or POSIX
  guardian: normal stop is EOF → SIGTERM → SIGKILL against only our child pid
  (the SDK `StdioClientTransport.close()` implements this order for CodeGraph;
  `common/src/supervisor.mjs` does it for tgrep).
- CodeGraph's library needs the built-in `node:sqlite` module (Node 22.5+ even
  though its package.json says >=20). Both the serve child and the first-index
  helper (`codegraph/src/initialize.mjs`) must run on the platform bundle's own
  Node (`node.exe` / `node`) with `--liftoff-only --disable-warning=ExperimentalWarning`.
  The POSIX `bin/codegraph` shell wrapper is never spawned (it re-injects
  `CODEGRAPH_HOST_PPID`).
- CodeGraph `serve --mcp` never creates the first index; the wrapper runs the
  library init helper when `<repo>/.codegraph-<project>-<platform>/codegraph.db`
  (or a valid `.codegraph/`) is missing. Tool schemas are taken from the
  backend's `tools/list` with `projectPath` stripped; the pinned static schemas
  are only the pre-ready fallback.
- CodeGraph 1.6.0 hides `codegraph_status` from `tools/list` for indexes with
  fewer than 500 files, even with an explicit tool allowlist. The wrapper
  probes that read-only handler before using its pinned schema; a missing
  handler still fails readiness. Keep both cases in protocol tests and run a
  real small-workspace smoke test.
- tgrep speaks newline JSON-RPC (`search`/`files`/`status`; `reload` never
  exposed) through the integration patch adding `--transport stdio`. The patch
  keeps `serve.lock` exclusivity and adds a backend-side 20 000-row cap.

## Upgrade flow (pinned upstream)

1. Update the tool's `upstream-lock.json` (commit, version, shasums). For a new
   tgrep pin, delete `<BUILD_DIR>/integration/mcp/upstream/tgrep-src` so prepare
   re-clones; re-apply the patch (regenerate it if it no longer fits) and record
   the new patch sha.
2. Re-verify the facts listed above against the new sources before changing
   wrapper code (especially stdin-EOF behavior, `CODEGRAPH_DIR` semantics, tool
   schemas, and the internal RPC shapes). The pinned npm artifacts are the
   authority when they differ from the inspected source tree (the tree has
   been ahead of the release before: 1.6.0 artifacts lack the `ui` subcommand
   the source has) — check the actual artifact's `--help`, not only the repo.
3. Run `node project/integration/mcp/setup.js` (or the prepare library) and the
   three test suites; then a real-backend smoke per README platform table.
4. Update the README verified-platform table with what was actually measured.

## Index and state rules

- tgrep index: `<BUILD_DIR>/integration/mcp/state/tgrep/<workspace-id>/<platform>/index`
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
- Wrapper state/locks: `wrapper-state.json` (atomic replace) and the O_EXCL
  instance lock with pid-liveness steal; backends' own locks (`serve.lock`,
  `writer.pid`) are the final exclusivity. Never kill by process name or stale
  pid file.

## Validation checklist

1. Unit tests in all three packages; protocol tests spawn the real server
   entry with a fake backend — keep them free of real-index dependencies.
2. Real-backend smoke (scratch script pattern under
   `<BUILD_DIR>/_agent_tmp/mcp/smoke.mjs`): first index, reuse start, one real
   query, client close → server and backend exit, no leftover processes, locks
   released.
3. For lifecycle changes, also verify the forced-kill path (kill the wrapper,
   observe backends exit via their stdin lifeline) and report timings.
