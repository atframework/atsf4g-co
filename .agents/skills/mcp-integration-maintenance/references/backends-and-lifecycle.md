# Backend Wrapper and Lifecycle Maintenance

Load only for wrapper-server, backend, patch, index, or upstream-upgrade work
under `project/integration/mcp/{tgrep,codegraph,common}`.

## Verified facts you may rely on (Plan.md section 2 has sources)

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
  library init helper when `<repo>/.codegraph-atsf4g-<platform>/codegraph.db`
  (or a valid `.codegraph/`) is missing. Tool schemas are taken from the
  backend's `tools/list` with `projectPath` stripped; the pinned static schemas
  are only the pre-ready fallback.
- tgrep speaks newline JSON-RPC (`search`/`files`/`status`; `reload` never
  exposed) through the integration patch adding `--transport stdio`. The patch
  keeps `serve.lock` exclusivity and adds a backend-side 20 000-row cap.

## Upgrade flow (pinned upstream)

1. Update the tool's `upstream-lock.json` (commit, version, shasums). For a new
   tgrep pin, delete `<BUILD_DIR>/integration/mcp/upstream/tgrep-src` so prepare
   re-clones; re-apply the patch (regenerate it if it no longer fits) and record
   the new patch sha.
2. Re-verify Plan.md section 2 claims against the new sources before changing
   wrapper code (especially stdin-EOF behavior, `CODEGRAPH_DIR` semantics, tool
   schemas, and the internal RPC shapes).
3. Run `node project/integration/mcp/setup.js` (or the prepare library) and the
   three test suites; then a real-backend smoke per README platform table.
4. Update Plan.md section 2/9 and the README verified-platform table with what
   was actually measured.

## Index and state rules

- tgrep index: `<BUILD_DIR>/integration/mcp/state/tgrep/<workspace-id>/<platform>/index`
  with one fixed exclude list; policy changes must stay identical across
  index/serve/watch or the index can drop members.
- CodeGraph index: repository-root `.codegraph-atsf4g-<platform>` (gitignored
  via `/.codegraph*`); an existing valid `.codegraph/` is reused in place.
  Windows and WSL must keep separate directories.
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
