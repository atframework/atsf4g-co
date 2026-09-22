#!/usr/bin/env node
/**
 * tgrep MCP wrapper server (stdio).
 *
 * Exposes exactly three tools -- tgrep_search, tgrep_files, tgrep_status --
 * against a privately supervised, patched `tgrep serve --transport stdio`
 * child. The tool list is fixed at startup so clients never cache an empty
 * set while the first index build runs; queries that arrive before the index
 * is ready fail with INDEX_NOT_READY instead of an empty success.
 *
 * Only MCP JSON-RPC is ever written to stdout; all diagnostics go to stderr.
 *
 * Usage (after common/tools/prepare.mjs):
 *   node tgrep/src/server.mjs [--repo-root R] [--build-dir B]
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

import { BackendError, ErrorCodes, IndexInUse } from '../../../common/src/errors.mjs';
import { LIMITS, clampContext, clampResultCount, fitsRequest, truncateRecords } from '../../../common/src/limits.mjs';
import {
  WorkspacePaths,
  deriveRepoRoot,
  resolveBuildDir,
  projectInfo,
  validateRelativeScope,
} from '../../../common/src/paths.mjs';
import { ServiceState, StateStore, ToolInstanceLock, currentIdentity } from '../../../common/src/state.mjs';
import { runWrapperServer, toolText } from '../../../common/src/mcpServer.mjs';
import { scriptInvocation } from '../../../common/src/runtime.mjs';
import { TgrepBackend } from './backend.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));

const TOOL_INSTRUCTIONS =
  'Fast regex and literal search over this repository via a locally supervised ' +
  'tgrep index. Tools never accept absolute paths: `scope` is a repository-relative ' +
  'subdirectory. Results report truncation explicitly; verify source before relying ' +
  'on matches for precise work.';

/** First-index budget: large trees take a while; status reports progress. */
const READY_BUDGET_MS = 60 * 60 * 1000;

function delay(ms) {
  return new Promise((resolve) => {
    setTimeout(resolve, ms);
  });
}

class TgrepService {
  constructor(paths, backendArgs) {
    this.paths = paths;
    this.backendArgs = backendArgs;
    this.state = ServiceState.STARTING;
    this.stateDetail = '';
    this.backend = null;
    this.lockError = null;
    this.ready = false;
    this.lock = new ToolInstanceLock(paths.toolStateDir('tgrep'), 'tgrep');
    this.identity = currentIdentity();
    this.stateStore = new StateStore(path.join(paths.toolStateDir('tgrep'), 'wrapper-state.json'));
    this.background = null;
  }

  /** Fast path for runWrapperServer: lock + spawn; verification is background. */
  startup() {
    try {
      this.lock.acquire(this.identity, this.paths.repoRoot, 'starting');
    } catch (error) {
      if (error instanceof IndexInUse) {
        this.lockError = error;
        this.state = ServiceState.FAILED;
        this.stateDetail = 'tool instance lock held by another wrapper';
        this.publishState();
        return;
      }
      throw error;
    }
    this.background = this.startBackend().catch((error) => {
      process.stderr.write(`tgrep-mcp: background startup failed: ${error?.message ?? error}\n`);
    });
  }

  async startBackend() {
    const backend = new TgrepBackend({
      paths: this.paths,
      binary: this.backendArgs.binary,
      argvOverride: this.backendArgs.argvOverride,
      stderrLog: path.join(this.paths.toolStateDir('tgrep'), 'backend.stderr.log'),
    });
    try {
      await backend.start();
    } catch (error) {
      if (error instanceof IndexInUse) {
        this.lockError = error;
      }
      this.state = ServiceState.FAILED;
      this.stateDetail = error instanceof BackendError ? error.message : String(error);
      this.publishState();
      return;
    }
    this.backend = backend;
    this.state = ServiceState.INITIALIZING;
    this.publishState();
    backend.watchExit((exit) => this.onBackendExit(exit));
    await this.waitUntilReady();
  }

  async waitUntilReady() {
    const deadline = Date.now() + READY_BUDGET_MS;
    while (Date.now() < deadline) {
      if (this.state === ServiceState.STOPPING || this.state === ServiceState.STOPPED || this.state === ServiceState.FAILED) {
        return;
      }
      try {
        const status = await this.backend.status();
        if (!status.indexing && status.hidden_complete) {
          this.state = ServiceState.READY;
          this.stateDetail = '';
          this.ready = true;
          this.publishState();
          return;
        }
        this.stateDetail = status.indexing ? 'initial index build in progress' : 'waiting for the index to become complete';
      } catch (error) {
        if (error instanceof BackendError && error.code === ErrorCodes.BACKEND_TIMEOUT) {
          this.stateDetail = 'backend slow to report status';
        } else {
          throw error;
        }
      }
      await delay(500);
    }
    this.state = ServiceState.DEGRADED;
    this.stateDetail = 'initialization did not complete within the startup budget';
    this.publishState();
  }

  onBackendExit(exit) {
    this.ready = false;
    if (this.state === ServiceState.STOPPING || this.state === ServiceState.STOPPED) {
      return;
    }
    this.state = ServiceState.DEGRADED;
    this.stateDetail = `backend exited unexpectedly (code ${exit?.code ?? null}, signal ${exit?.signal ?? null})`;
    this.publishState();
  }

  publishState(errorCode = null) {
    const payload = {
      tool: 'tgrep',
      state: this.state,
      state_detail: this.stateDetail,
      identity: this.identity,
      repo_root: this.paths.repoRoot,
      error_code: errorCode,
      updated_unix: Math.floor(Date.now() / 1000),
    };
    try {
      this.stateStore.write(payload);
    } catch {
      /* diagnostics only */
    }
    try {
      this.lock.updateStage(this.state);
    } catch {
      /* diagnostics only */
    }
  }

  requireBackend() {
    if (this.lockError) {
      throw new BackendError(
        ErrorCodes.INDEX_IN_USE,
        'another tgrep MCP instance owns this index; this instance will not serve queries'
      );
    }
    if (!this.ready || this.backend === null || !this.backend.alive()) {
      if (this.state === ServiceState.INITIALIZING || this.state === ServiceState.STARTING) {
        throw new BackendError(
          ErrorCodes.INDEX_NOT_READY,
          'the tgrep index is still initializing; retry after tgrep_status reports ready'
        );
      }
      throw new BackendError(
        ErrorCodes.BACKEND_FAILED,
        `the tgrep backend is not running; ${this.stateDetail || 'see wrapper state'}`
      );
    }
    return this.backend;
  }

  async shutdown() {
    if (this.state === ServiceState.STOPPING || this.state === ServiceState.STOPPED) {
      return;
    }
    this.state = ServiceState.STOPPING;
    this.publishState();
    if (this.backend) {
      await this.backend.stop();
      this.backend = null;
    }
    this.state = ServiceState.STOPPED;
    this.publishState();
    this.lock.release();
  }
}

// -- tool handlers ----------------------------------------------------------------

function makeTools(service) {
  function invalid(message) {
    return new BackendError(ErrorCodes.INVALID_PARAMS, message);
  }

  async function tgrepSearch(args) {
    const backend = service.requireBackend();
    const pattern = args?.pattern;
    if (typeof pattern !== 'string' || pattern === '') {
      throw invalid('pattern must be a non-empty string');
    }
    if (pattern.length > LIMITS.maxPatternLength) {
      throw new BackendError(ErrorCodes.REQUEST_TOO_LARGE, 'pattern exceeds the length limit');
    }
    if (!fitsRequest(pattern)) {
      throw new BackendError(ErrorCodes.REQUEST_TOO_LARGE, 'request exceeds the size limit');
    }
    let scope;
    try {
      scope = validateRelativeScope(args?.scope);
    } catch (error) {
      throw invalid(error.message);
    }
    const globEntries = Array.isArray(args?.glob) ? args.glob.filter((entry) => typeof entry === 'string' && entry !== '') : [];
    if (globEntries.length > 4) {
      throw invalid('at most 4 glob filters are allowed');
    }
    const context = clampContext(args?.context);
    const rowLimit = clampResultCount(args?.max_results, LIMITS.maxMatchRows);

    const params = {
      pattern,
      fixed_string: args?.fixed_string === true,
      case_insensitive: args?.case_insensitive === true,
      word_boundary: args?.word_boundary === true,
      scope,
      before_context: context,
      after_context: context,
      detail: true,
      positions: false,
    };
    if (globEntries.length > 0) {
      params.glob = globEntries;
    }
    const result = await backend.search(params);
    const matches = Array.isArray(result.matches) ? result.matches : [];
    const { kept, truncated } = truncateRecords(matches, rowLimit);
    return toolText({
      matches: kept,
      num_matches: kept.length,
      truncated: Boolean(truncated) || Boolean(result.truncated),
      elapsed_ms: result.elapsed_ms,
    });
  }

  async function tgrepFiles(args) {
    const backend = service.requireBackend();
    let scope;
    try {
      scope = validateRelativeScope(args?.scope);
    } catch (error) {
      throw invalid(error.message);
    }
    const globEntries = Array.isArray(args?.glob) ? args.glob.filter((entry) => typeof entry === 'string' && entry !== '') : [];
    const requested = args?.limit === undefined || args?.limit === null ? LIMITS.maxFileEntries : Number(args.limit);
    const entryLimit = Math.max(1, Math.min(Number.isFinite(requested) ? Math.trunc(requested) : 1, LIMITS.maxFileEntries));

    const params = { scope };
    if (globEntries.length > 0) {
      params.glob = globEntries;
    }
    const result = await backend.files(params);
    const files = Array.isArray(result.files) ? result.files : [];
    const kept = files.slice(0, entryLimit);
    return toolText({
      files: kept,
      num_files: kept.length,
      truncated: kept.length < files.length,
    });
  }

  async function tgrepStatus() {
    const payload = {
      tool: 'tgrep',
      wrapper_state: service.state,
      state_detail: service.stateDetail,
    };
    if (service.lockError) {
      payload.error = service.lockError.toPayload();
      return toolText(payload);
    }
    if (service.backend && service.backend.alive()) {
      try {
        const status = await service.backend.status();
        status.index_dir_managed_by = 'wrapper';
        payload.backend = status;
      } catch (error) {
        if (error instanceof BackendError) {
          payload.error = error.toPayload();
        } else {
          throw error;
        }
      }
    }
    return toolText(payload);
  }

  return [
    {
      name: 'tgrep_search',
      description:
        'Search the repository index with a regex (or literal with fixed_string=true). ' +
        'Returns file/line/content match rows plus match spans; `scope` is a ' +
        'repo-relative subdirectory. Results are capped and report `truncated`.',
      inputSchema: {
        type: 'object',
        properties: {
          pattern: { type: 'string', description: 'Regex pattern (or literal with fixed_string=true)' },
          scope: { type: 'string', description: 'Repository-relative subdirectory to search in' },
          fixed_string: { type: 'boolean', description: 'Treat the pattern as a literal string' },
          case_insensitive: { type: 'boolean', description: 'Case-insensitive matching' },
          word_boundary: { type: 'boolean', description: 'Match on word boundaries' },
          glob: { type: 'array', items: { type: 'string' }, maxItems: 4, description: 'File glob filters (e.g. "*.cpp")' },
          context: { type: 'integer', minimum: 0, maximum: LIMITS.maxContextLines, description: 'Context lines per side (0-3)' },
          max_results: { type: 'integer', minimum: 1, maximum: LIMITS.maxMatchRows, description: 'Global match-row cap' },
        },
        required: ['pattern'],
      },
      handler: tgrepSearch,
    },
    {
      name: 'tgrep_files',
      description: 'List indexed files under a repo-relative scope, optionally glob-filtered.',
      inputSchema: {
        type: 'object',
        properties: {
          scope: { type: 'string', description: 'Repository-relative subdirectory' },
          glob: { type: 'array', items: { type: 'string' }, maxItems: 4, description: 'File glob filters' },
          limit: { type: 'integer', minimum: 1, description: 'Maximum number of entries' },
        },
      },
      handler: tgrepFiles,
    },
    {
      name: 'tgrep_status',
      description:
        'Report wrapper lifecycle state and tgrep index/watcher status ' +
        '(indexing progress, watch mode, fallback reason).',
      inputSchema: { type: 'object', properties: {} },
      handler: tgrepStatus,
    },
  ];
}

// -- entry point ------------------------------------------------------------------

function resolveBinary(paths, explicit) {
  if (explicit) {
    return path.resolve(explicit);
  }
  const fakeScript = process.env.TGREP_MCP_FAKE_SCRIPT;
  if (fakeScript) {
    return path.resolve(fakeScript); // test double; never executed directly
  }
  let binary = null;
  const prepared = new StateStore(paths.preparedStatePath()).read();
  if (prepared?.tgrep?.binary) {
    binary = prepared.tgrep.binary;
  }
  if (!binary) {
    const name = process.platform === 'win32' ? 'tgrep.exe' : 'tgrep';
    const legacy = path.join(paths.integrationDir, 'runtime', name);
    binary = fs.existsSync(path.join(paths.runtimeDir, name)) ? path.join(paths.runtimeDir, name) : legacy;
  }
  if (!fs.existsSync(binary)) {
    process.stderr.write('tgrep-mcp: prepared tgrep binary not found; run common/tools/prepare.mjs first\n');
    process.exit(2);
  }
  return binary;
}

function main() {
  const { values } = parseArgs({
    options: {
      'repo-root': { type: 'string' },
      'build-dir': { type: 'string' },
      'tgrep-binary': { type: 'string' },
    },
  });

  const repoRoot = deriveRepoRoot(import.meta.url, values['repo-root']);
  const buildDir = resolveBuildDir(repoRoot, values['build-dir']);
  const paths = new WorkspacePaths(repoRoot, buildDir);
  paths.ensureDirs();

  const fakeScript = process.env.TGREP_MCP_FAKE_SCRIPT;
  const fakeInvocation = fakeScript ? scriptInvocation(path.resolve(fakeScript)) : null;
  const argvOverride = fakeInvocation ? [fakeInvocation.command, ...fakeInvocation.args] : null;
  const binary = resolveBinary(paths, values['tgrep-binary']);

  const service = new TgrepService(paths, { binary, argvOverride });
  void runWrapperServer({
    name: `${projectInfo(repoRoot).slug}-tgrep`,
    instructions: TOOL_INSTRUCTIONS,
    tools: makeTools(service),
    service,
  });
  return service;
}

main();
