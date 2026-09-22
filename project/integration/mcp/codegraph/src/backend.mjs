/**
 * CodeGraph backend adapter.
 *
 * Owns the pinned CodeGraph `serve --mcp --path <root>` child in direct mode
 * (`CODEGRAPH_NO_DAEMON=1`) with telemetry, update checks, and downloads
 * disabled. The child is spawned and supervised by the SDK's
 * StdioClientTransport: argument array, fixed cwd, minimal environment merged
 * with the forced variables below, and a stop ordering of stdin EOF ->
 * SIGTERM -> SIGKILL (2s/2s) built into transport.close().
 *
 * First-run indexing runs through initialize.mjs (library init + indexAll, no
 * CLI git-hook fallback) on the SAME verified Node as the serve child: the
 * library needs the built-in node:sqlite module (Node 22.5+), which the
 * system Node need not provide when a platform package supplies its own runtime.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';

import { BackendError, ErrorCodes } from '../../common/src/errors.mjs';
import { LIMITS } from '../../common/src/limits.mjs';
import { defaultIndexDirName, selectCodegraphIndex, projectInfo } from '../../common/src/paths.mjs';
import { StderrSink, minimalEnvironment, supervise } from '../../common/src/supervisor.mjs';
import { CODEGRAPH_NODE_FLAGS } from '../../common/src/localTools.mjs';

const INITIALIZER = path.join(path.dirname(fileURLToPath(import.meta.url)), 'initialize.mjs');

// Mirror the bundle's own launchers (npm shim / bin/codegraph): the bundled
// Node runs with the WASM turboshaft guard and the node:sqlite warning muted,
// for both the serve child and the first-index helper.
export const BUNDLED_NODE_FLAGS = CODEGRAPH_NODE_FLAGS;

export const DEFAULT_TOOL_ALLOWLIST = ['codegraph_explore', 'codegraph_status'];
export const EXTRA_TOOLS = [
  'codegraph_search',
  'codegraph_callers',
  'codegraph_callees',
  'codegraph_impact',
  'codegraph_node',
  'codegraph_files',
];

const FORCED_ENV_BASE = {
  CODEGRAPH_NO_DAEMON: '1',
  DO_NOT_TRACK: '1',
  CODEGRAPH_TELEMETRY: '0',
  CODEGRAPH_NO_UPDATE_CHECK: '1',
  CODEGRAPH_NO_DOWNLOAD: '1',
  // The wrapper may run the first-index helper before the MCP handshake;
  // the upstream no-traffic self-shutdown must not fire.
  CODEGRAPH_STARTUP_HANDSHAKE_TIMEOUT_MS: '0',
};
const CLEARED_ENV = [
  'CODEGRAPH_DAEMON_INTERNAL',
  'CODEGRAPH_MCP_DEBUG',
  'CODEGRAPH_TELEMETRY_ENDPOINT',
  'NODE_OPTIONS',
  'NODE_PATH',
  'CODEGRAPH_HOST_PPID',
];

export function indexDirName(repoRoot = process.cwd()) {
  return defaultIndexDirName(repoRoot);
}

/**
 * Where does the index live, and does the wrapper need to create it?
 * Reuses a valid upstream default `.codegraph` in place; falls back to the
 * wrapper-managed per-platform directory (CODEGRAPH_DIR semantics).
 */
export function resolveIndexSelection(repoRoot) {
  return selectCodegraphIndex(repoRoot);
}

/**
 * Run the one-shot first-index helper (library init + indexAll) as a
 * supervised child on the verified runtime (node:sqlite needs Node 22.5+).
 * Resolves with the final parsed progress/done line.
 */
export async function runInitializer({ runtime, repoRoot, libraryDir, dirName, stderrLog, signal }) {
  signal?.throwIfAborted();
  const stderrSink = new StderrSink({ filePath: stderrLog });
  const env = minimalEnvironment({
    ...FORCED_ENV_BASE,
    ...(dirName ? { CODEGRAPH_DIR: dirName } : {}),
  });
  const proc = supervise({
    argv: [runtime, ...BUNDLED_NODE_FLAGS, INITIALIZER, libraryDir, repoRoot],
    cwd: repoRoot,
    env,
    label: 'codegraph-init',
    stderrSink,
  });
  let lastProgress = null;
  let doneLine = null;
  let pendingOutput = '';
  const abort = () => { void proc.stop(); };
  signal?.addEventListener('abort', abort, { once: true });
  proc.child.stdout.setEncoding('utf8');
  proc.child.stdout.on('data', (chunk) => {
    pendingOutput += chunk;
    const lines = pendingOutput.split('\n');
    pendingOutput = lines.pop();
    for (const line of lines) {
      if (!line.trim()) {
        continue;
      }
      try {
        const message = JSON.parse(line);
        if (message.type === 'progress') {
          lastProgress = message;
        } else if (message.type === 'done') {
          doneLine = message;
        }
      } catch {
        /* non-JSON helper noise */
      }
    }
  });
  const exit = await proc.exited;
  signal?.removeEventListener('abort', abort);
  signal?.throwIfAborted();
  if (exit.code !== 0 || !doneLine?.ok) {
    throw new BackendError(ErrorCodes.BACKEND_FAILED, 'CodeGraph first-index helper failed', {
      exit_code: exit.code,
      helper_error: doneLine?.error ?? null,
      stderr_tail: stderrSink.excerpt(500),
    });
  }
  return { done: doneLine, lastProgress };
}

export class CodeGraphBackend {
  /**
   * @param {object} options
   * @param {import('../../common/src/paths.mjs').WorkspacePaths} options.paths
   * @param {string} options.runtime verified Node executable
   * @param {string|null} options.cliEntry compiled dist/bin/codegraph.js entry
   * @param {string[]|null} [options.argvOverride] test double argv (not upstream code)
   * @param {string[]|null} [options.extraTools]
   * @param {string|null} [options.dirName] CODEGRAPH_DIR value or null for default
   * @param {string|null} [options.stderrLog]
   */
  constructor({ paths, runtime, cliEntry, argvOverride = null, extraTools = [], dirName = indexDirName(paths.repoRoot), stderrLog = null }) {
    this.paths = paths;
    this.runtime = runtime;
    this.cliEntry = cliEntry;
    this.argvOverride = argvOverride;
    this.extraTools = extraTools.filter((name) => EXTRA_TOOLS.includes(name));
    this.dirName = dirName;
    this.stderrSink = new StderrSink({ filePath: stderrLog });
    this.transport = null;
    this.client = null;
    this.onExit = null;
    this.onSyncChange = null;
    this.sync = { watcher: 'starting', catch_up_on_connect: true, last_sync_unix: null, last_error: null };
    this.pendingDiagnostics = '';
  }

  syncStatus() { return { mode: 'session', ...this.sync }; }

  observeDiagnostics(chunk) {
    this.pendingDiagnostics += chunk.toString();
    const lines = this.pendingDiagnostics.split('\n');
    this.pendingDiagnostics = lines.pop().slice(-4096);
    for (const line of lines) {
      if (!line.startsWith('[CodeGraph MCP] ')) continue;
      let changed = true;
      if (line.includes('File watcher active')) this.sync.watcher = 'active';
      else if (line.includes('File watcher disabled')) { this.sync.watcher = 'disabled'; this.sync.last_error = line.trim(); }
      else if (line.includes('File watcher unavailable')) { this.sync.watcher = 'unavailable'; this.sync.last_error = line.trim(); }
      else if (line.includes('File watcher degraded')) { this.sync.watcher = 'degraded'; this.sync.last_error = line.trim(); }
      else if (line.includes('Auto-sync error:') || line.includes('Catch-up sync failed:')) this.sync.last_error = line.trim();
      else if (line.includes('Auto-synced ') || line.includes('Caught up ')) {
        this.sync.last_sync_unix = Math.floor(Date.now() / 1000);
        this.sync.last_sync_message = line.trim();
        this.sync.last_error = null;
      } else changed = false;
      if (changed) this.onSyncChange?.();
    }
  }

  allowlist() {
    return [...DEFAULT_TOOL_ALLOWLIST, ...this.extraTools];
  }

  buildSpawnSpec() {
    // Serve on the bundled Node via the app's JS entry, mirroring the
    // bundle's own launchers on both platforms (never the POSIX shell
    // wrapper, which would re-inject CODEGRAPH_HOST_PPID).
    const argv = this.argvOverride
      ? [...this.argvOverride]
      : [this.runtime, ...BUNDLED_NODE_FLAGS, this.cliEntry];
    argv.push('serve', '--mcp', '--path', this.paths.repoRoot);

    // Test doubles are not upstream code; they need the parent environment.
    const base = this.argvOverride ? { ...process.env } : minimalEnvironment();
    const env = {
      ...base,
      ...FORCED_ENV_BASE,
      CODEGRAPH_MCP_TOOLS: this.allowlist().join(','),
    };
    if (this.dirName) {
      env.CODEGRAPH_DIR = this.dirName;
    } else {
      delete env.CODEGRAPH_DIR;
    }
    for (const name of CLEARED_ENV) {
      delete env[name];
    }
    return { argv, env };
  }

  async start() {
    const { argv, env } = this.buildSpawnSpec();
    this.transport = new StdioClientTransport({
      command: argv[0],
      args: argv.slice(1),
      env,
      cwd: this.paths.repoRoot,
      stderr: 'pipe',
      maxBufferSize: LIMITS.maxBackendFrameBytes,
    });
    this.transport.stderr?.setEncoding('utf8');
    this.transport.stderr?.on('data', (chunk) => { this.stderrSink.write(chunk); this.observeDiagnostics(chunk); });
    this.transport.onclose = () => this.onExit?.();
    this.client = new Client({ name: `${projectInfo(this.paths.repoRoot).slug}-codegraph-wrapper`, version: '0.1.0' });
    try {
      await this.client.connect(this.transport); // initialize handshake
    } catch (error) {
      throw new BackendError(ErrorCodes.BACKEND_FAILED, `CodeGraph backend handshake failed: ${error?.message ?? error}`, {
        stderr_tail: this.stderrSink.excerpt(500),
      });
    }
  }

  alive() {
    return this.client !== null && this.transport !== null && this.transport.pid !== null;
  }

  async listTools() {
    return this.client.listTools({ timeout: LIMITS.statusTimeoutMs });
  }

  async callTool(name, args) {
    return this.client.callTool({ name, arguments: args }, { timeout: LIMITS.queryTimeoutMs });
  }

  async stop() {
    if (this.client) {
      // SDK transport close: stdin EOF -> 2s -> SIGTERM -> 2s -> SIGKILL.
      await this.client.close();
      this.client = null;
      this.transport = null;
    }
  }
}
