/**
 * tgrep backend adapter: owns the patched `tgrep serve --transport stdio`
 * child. The binary, index directory, and scan policy are fixed at
 * construction from the prepared state; tool parameters can never change the
 * repository, the index location, or the scan policy.
 *
 * Lifecycle: spawn under the shared supervisor (argument array, fixed cwd,
 * minimal environment), poll the internal `status` RPC until it answers,
 * expose search/files/status through the line RPC client, and stop with the
 * supervisor's EOF-first ordering.
 */

import path from 'node:path';
import fs from 'node:fs';

import { BackendError, ErrorCodes } from '../../../common/src/errors.mjs';
import { LIMITS } from '../../../common/src/limits.mjs';
import { LineJsonRpcClient } from '../../../common/src/lineRpc.mjs';
import { StderrSink, minimalEnvironment, supervise } from '../../../common/src/supervisor.mjs';
import { isUnrealWorkspace, UNREAL_EXCLUDE_DIRS } from '../../../common/src/scanPolicy.mjs';

// Scan policy shared by every start mode (index build, catch-up, watcher):
// keep the vendored framework sources, drop build output, dependency
// installs, and both tools' index directories. Values are directory names.
export const TGREP_EXCLUDE_DIRS = [
  '.git',
  'node_modules',
  'build',
  'target',
  'install',
  'packages',
  '.cache',
  '.tgrep',
  '.codegraph',
];

const RESPONSIVE_TIMEOUT_MS = 30_000;

function delay(ms) {
  return new Promise((resolve) => {
    setTimeout(resolve, ms);
  });
}

export class TgrepBackend {
  /**
   * @param {object} options
   * @param {import('../../../common/src/paths.mjs').WorkspacePaths} options.paths
   * @param {string} options.binary absolute path to the patched tgrep binary
   * @param {string[]|null} [options.argvOverride] test double argv (not upstream code)
   * @param {string|null} [options.stderrLog]
   */
  constructor({ paths, binary, argvOverride = null, stderrLog = null }) {
    this.paths = paths;
    this.binary = binary;
    this.argvOverride = argvOverride;
    this.indexDir = path.join(paths.toolStateDir('tgrep'), 'index');
    this.proc = null;
    this.rpc = null;
    this.stderrSink = new StderrSink({ filePath: stderrLog });
  }

  buildArgv() {
    if (this.argvOverride) {
      return [...this.argvOverride];
    }
    const argv = [
      this.binary,
      'serve',
      this.paths.repoRoot,
      '--transport',
      'stdio',
      '--index-path',
      this.indexDir,
      '--no-require-git',
    ];
    const excludes = isUnrealWorkspace(this.paths.repoRoot)
      ? [...UNREAL_EXCLUDE_DIRS]
      : [...TGREP_EXCLUDE_DIRS];
    excludes.push(...fs.readdirSync(this.paths.repoRoot).filter(name => name.startsWith('.codegraph-')));
    const buildRelative = path.relative(this.paths.repoRoot, this.paths.buildDir);
    if (buildRelative && !buildRelative.startsWith('..') && !path.isAbsolute(buildRelative)) excludes.push(path.basename(this.paths.buildDir));
    for (const exclude of new Set(excludes)) {
      argv.push('--exclude', exclude);
    }
    return argv;
  }

  async start() {
    // Test doubles are not upstream code; they need the parent environment.
    const env = this.argvOverride ? { ...process.env } : minimalEnvironment();
    this.proc = supervise({
      argv: this.buildArgv(),
      cwd: this.paths.repoRoot,
      env,
      label: 'tgrep-serve',
      stderrSink: this.stderrSink,
    });
    this.rpc = new LineJsonRpcClient(this.proc);
    await this.waitUntilResponsive();
  }

  async waitUntilResponsive(timeoutMs = RESPONSIVE_TIMEOUT_MS) {
    const deadline = Date.now() + timeoutMs;
    let lastError = null;
    while (Date.now() < deadline) {
      if (!this.proc.alive()) {
        throw new BackendError(ErrorCodes.BACKEND_FAILED, 'tgrep backend exited during startup', {
          stderr_tail: this.stderrSink.excerpt(500),
        });
      }
      try {
        await this.rpc.request('status', {}, 2000);
        return;
      } catch (error) {
        lastError = error;
        if (error.code === ErrorCodes.INDEX_NOT_READY || error.code === ErrorCodes.BACKEND_TIMEOUT) {
          await delay(200);
          continue;
        }
        throw error;
      }
    }
    throw new BackendError(ErrorCodes.BACKEND_FAILED, 'tgrep backend did not answer the status probe', {
      last_error: lastError ? lastError.code : null,
      stderr_tail: this.stderrSink.excerpt(500),
    });
  }

  async stop() {
    if (this.proc) {
      await this.proc.stop();
      this.proc = null;
    }
    this.rpc = null;
  }

  alive() {
    return this.proc !== null && this.proc.alive();
  }

  /** Invoke `callback` once with the backend's exit info (unexpected or not). */
  watchExit(callback) {
    if (this.proc) {
      this.proc.exited.then(callback);
    }
  }

  async status() {
    const result = await this.rpc.request('status', {}, LIMITS.statusTimeoutMs);
    if (result === null || typeof result !== 'object') {
      throw new BackendError(ErrorCodes.BACKEND_FAILED, 'malformed status response');
    }
    return result;
  }

  async search(params) {
    const result = await this.rpc.request('search', params);
    if (result === null || typeof result !== 'object') {
      throw new BackendError(ErrorCodes.BACKEND_FAILED, 'malformed search response');
    }
    return result;
  }

  async files(params) {
    const result = await this.rpc.request('files', params);
    if (result === null || typeof result !== 'object') {
      throw new BackendError(ErrorCodes.BACKEND_FAILED, 'malformed files response');
    }
    return result;
  }
}
