#!/usr/bin/env node
/**
 * CodeGraph MCP wrapper server (stdio).
 *
 * Bridges an MCP client (the agent) to a supervised CodeGraph direct-mode
 * backend (pinned bundle runtime). Security-relevant behavior:
 *
 *   * The tool list is the fixed allowlist (default codegraph_explore plus a
 *     wrapped codegraph_status). Extra tools come from local static
 *     configuration only, never from MCP parameters.
 *   * Every call strips and rejects `projectPath`: the backend always
 *     receives the startup-verified repository root. Unknown top-level
 *     arguments are rejected rather than forwarded.
 *   * Schemas come from the backend's own tools/list (projectPath removed);
 *     the pinned static schemas below are the fallback until it answers and
 *     the drift check at startup.
 *
 * Only MCP JSON-RPC is written to stdout; diagnostics go to stderr.
 *
 * Usage (after common/tools/prepare.mjs):
 *   node codegraph/src/server.mjs [--repo-root R] [--build-dir B]
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

import { BackendError, ErrorCodes, IndexInUse } from '../../common/src/errors.mjs';
import { WorkspacePaths, deriveRepoRoot, resolveBuildDir, validateRelativeScope, projectInfo } from '../../common/src/paths.mjs';
import { ServiceState, StateStore, ToolInstanceLock, currentIdentity } from '../../common/src/state.mjs';
import { runWrapperServer, toolText } from '../../common/src/mcpServer.mjs';
import {
  DEFAULT_TOOL_ALLOWLIST,
  EXTRA_TOOLS,
  CodeGraphBackend,
  indexDirName,
  resolveIndexSelection,
  runInitializer,
} from './backend.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));

const TOOL_INSTRUCTIONS =
  'Structural code navigation (symbols, references, impact) for this repository, ' +
  'backed by a locally supervised CodeGraph index. Structural results are ' +
  'syntax-level, not compiler semantics: verify against source before relying on ' +
  'call relationships. All queries are scoped to this repository.';

const READ_ONLY_ANNOTATIONS = Object.freeze({
  readOnlyHint: true,
  destructiveHint: false,
  idempotentHint: true,
  openWorldHint: false,
});

/** Fallback schemas for the pinned upstream version (projectPath removed).
 *  Verified against tools.ts at commit ba3c21e; refreshed from the live
 *  backend once it answers tools/list. */
const STATIC_TOOL_SCHEMAS = Object.freeze({
  codegraph_explore: {
    type: 'object',
    properties: {
      query: { type: 'string', description: 'Symbol names, file names, short code terms, or a natural-language question to explore.' },
      maxFiles: { type: 'number', description: 'Maximum number of files to include source code from (default: 12)', default: 12 },
    },
    required: ['query'],
  },
  codegraph_search: {
    type: 'object',
    properties: {
      query: { type: 'string', description: 'Symbol name or partial name.' },
      kind: { type: 'string', enum: ['function', 'method', 'class', 'interface', 'type', 'variable', 'route', 'component'] },
      limit: { type: 'number', description: 'Maximum results (default: 10)', default: 10 },
    },
    required: ['query'],
  },
  codegraph_callers: {
    type: 'object',
    properties: {
      symbol: { type: 'string', description: 'Name of the function, method, or class to find callers for.' },
      file: { type: 'string', description: 'Narrow to the definition in this file (path or suffix).' },
      limit: { type: 'number', description: 'Maximum number of callers (default: 20)', default: 20 },
    },
    required: ['symbol'],
  },
  codegraph_callees: {
    type: 'object',
    properties: {
      symbol: { type: 'string', description: 'Name of the function, method, or class to find callees for.' },
      file: { type: 'string', description: 'Narrow to the definition in this file (path or suffix).' },
      limit: { type: 'number', description: 'Maximum number of callees (default: 20)', default: 20 },
    },
    required: ['symbol'],
  },
  codegraph_impact: {
    type: 'object',
    properties: {
      symbol: { type: 'string', description: 'Name of the symbol to analyze impact for.' },
      file: { type: 'string', description: 'Narrow to the definition in this file (path or suffix).' },
      depth: { type: 'number', description: 'How many levels of dependencies to traverse (default: 2)', default: 2 },
    },
    required: ['symbol'],
  },
  codegraph_node: {
    type: 'object',
    properties: {
      symbol: { type: 'string', description: 'Name of the symbol to read (symbol mode).' },
      includeCode: { type: 'boolean', description: 'Symbol mode: include the symbol full body (default: false).', default: false },
      file: { type: 'string', description: 'A file path or basename; alone = read the file, with symbol = disambiguate.' },
      offset: { type: 'number', description: 'File mode: 1-based line to start from.' },
      limit: { type: 'number', description: 'File mode: maximum lines to return (capped at 2000).' },
      symbolsOnly: { type: 'boolean', description: 'File mode: return the symbol map instead of source.', default: false },
      line: { type: 'number', description: 'Symbol mode: disambiguate to the definition at this line.' },
    },
    required: [],
  },
  codegraph_files: {
    type: 'object',
    properties: {
      path: { type: 'string', description: 'Filter to files under this directory path.' },
      pattern: { type: 'string', description: 'Filter files matching this glob pattern.' },
      format: { type: 'string', enum: ['tree', 'flat', 'grouped'], default: 'tree' },
      includeMetadata: { type: 'boolean', description: 'Include language and symbol counts (default: true).', default: true },
      maxDepth: { type: 'number', description: 'Maximum directory depth to show.' },
    },
  },
  codegraph_status: { type: 'object', properties: {} },
});

const TOOL_DESCRIPTIONS = Object.freeze({
  codegraph_explore: 'Find symbols relevant to a natural-language query or task in this repository.',
  codegraph_search: 'Search indexed symbols by name/kind in this repository.',
  codegraph_callers: 'List callers of a symbol (syntax-level).',
  codegraph_callees: 'List callees of a symbol (syntax-level).',
  codegraph_impact: 'Estimate the impact set of changing a symbol (syntax-level).',
  codegraph_node: 'Look up one symbol, or read one indexed file (syntax-level).',
  codegraph_files: 'List indexed files.',
});

/** Remove projectPath from a backend tool schema; return null on unusable input. */
function deriveSchema(upstreamSchema) {
  if (!upstreamSchema || typeof upstreamSchema !== 'object') {
    return null;
  }
  const schema = structuredClone(upstreamSchema);
  if (schema.properties && typeof schema.properties === 'object') {
    delete schema.properties.projectPath;
  }
  if (Array.isArray(schema.required)) {
    schema.required = schema.required.filter((name) => name !== 'projectPath');
    if (schema.required.length === 0) {
      delete schema.required;
    }
  }
  return schema;
}

class CodeGraphService {
  constructor(paths, backendArgs) {
    this.paths = paths;
    this.backendArgs = backendArgs;
    this.state = ServiceState.STARTING;
    this.stateDetail = '';
    this.backend = null;
    this.lockError = null;
    this.backendSchemas = new Map();
    this.schemaSource = 'static';
    this.allowlist = [...DEFAULT_TOOL_ALLOWLIST, ...CodeGraphService.loadExtraTools(paths)];
    this.lock = new ToolInstanceLock(paths.toolStateDir('codegraph'), 'codegraph');
    this.identity = currentIdentity();
    this.stateStore = new StateStore(path.join(paths.toolStateDir('codegraph'), 'wrapper-state.json'));
  }

  static loadExtraTools(paths) {
    const configPath = path.join(paths.toolStateDir('codegraph'), 'extra-tools.json');
    try {
      const raw = JSON.parse(fs.readFileSync(configPath, 'utf8'));
      if (raw && Array.isArray(raw.enabled_tools)) {
        return raw.enabled_tools.filter((name) => EXTRA_TOOLS.includes(name));
      }
    } catch {
      /* default allowlist */
    }
    return [];
  }

  /** Fast path for runWrapperServer: lock only; bring-up runs in background. */
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
    this.startBackend().catch((error) => {
      process.stderr.write(`codegraph-mcp: background startup failed: ${error?.message ?? error}\n`);
    });
  }

  async startBackend() {
    const { runtime, cliEntry, libraryDir, argvOverride } = this.backendArgs;
    let dirName = indexDirName();
    if (!argvOverride) {
      const selection = resolveIndexSelection(this.paths.repoRoot);
      dirName = selection.dirName;
      if (selection.needsInit) {
        this.state = ServiceState.INITIALIZING;
        this.stateDetail = 'running the first full index';
        this.publishState();
        try {
          const result = await runInitializer({
            runtime,
            repoRoot: this.paths.repoRoot,
            libraryDir,
            dirName,
            stderrLog: path.join(this.paths.toolStateDir('codegraph'), 'init.stderr.log'),
          });
          this.stateDetail = `first index done (${result.done.stats?.files ?? '?'} files)`;
        } catch (error) {
          this.state = ServiceState.DEGRADED;
          this.stateDetail = `first index failed: ${error.message}`;
          this.publishState();
          return;
        }
      }
    }

    this.state = ServiceState.OPENING;
    this.publishState();
    const backend = new CodeGraphBackend({
      paths: this.paths,
      runtime,
      cliEntry,
      argvOverride,
      extraTools: this.allowlist.filter((name) => EXTRA_TOOLS.includes(name)),
      dirName,
      stderrLog: path.join(this.paths.toolStateDir('codegraph'), 'backend.stderr.log'),
    });
    backend.onExit = () => this.onBackendExit();
    try {
      await backend.start();
    } catch (error) {
      this.state = ServiceState.DEGRADED;
      this.stateDetail = error.message;
      this.publishState();
      return;
    }
    this.backend = backend;

    try {
      const listed = await backend.listTools();
      const byName = new Map((listed.tools ?? []).map((tool) => [tool.name, tool]));
      // Pinned 1.6.0 hides status from tools/list for projects below 500 files,
      // even with an explicit allowlist, while retaining its read-only handler.
      // Our status endpoint has a local schema. Verify the handler before
      // accepting this discovery omission; an absent/broken handler still fails.
      if (!byName.has('codegraph_status')) {
        const status = await backend.callTool('codegraph_status', {});
        if (!status.isError) byName.set('codegraph_status', { name: 'codegraph_status', inputSchema: STATIC_TOOL_SCHEMAS.codegraph_status });
      }
      const missing = this.allowlist.filter((name) => !byName.has(name));
      if (missing.length > 0) {
        this.state = ServiceState.DEGRADED;
        this.stateDetail = `backend does not expose allowlisted tools: ${missing.join(', ')}`;
        this.publishState();
        return;
      }
      this.backendSchemas = byName;
      this.schemaSource = 'backend';
      this.state = ServiceState.READY;
      this.stateDetail = '';
    } catch (error) {
      this.state = ServiceState.DEGRADED;
      this.stateDetail = `backend tools/list failed: ${error?.message ?? error}`;
    }
    this.publishState();
  }

  onBackendExit() {
    if (this.state === ServiceState.STOPPING || this.state === ServiceState.STOPPED) {
      return;
    }
    this.state = ServiceState.DEGRADED;
    this.stateDetail = 'backend exited unexpectedly';
    this.publishState();
  }

  publishState(errorCode = null) {
    const payload = {
      tool: 'codegraph',
      state: this.state,
      state_detail: this.stateDetail,
      identity: this.identity,
      repo_root: this.paths.repoRoot,
      index_dir_name: this.backendArgs.argvOverride ? null : this.currentDirName(),
      allowlist: this.allowlist,
      schema_source: this.schemaSource,
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

  currentDirName() {
    if (this.backend) {
      return this.backend.dirName;
    }
    const selection = resolveIndexSelection(this.paths.repoRoot);
    return selection.dirName;
  }

  requireBackend() {
    if (this.lockError) {
      throw new BackendError(
        ErrorCodes.INDEX_IN_USE,
        'another codegraph MCP instance owns this index; this instance will not serve queries'
      );
    }
    if (this.state !== ServiceState.READY || this.backend === null) {
      const detail = this.stateDetail || `wrapper state is ${this.state}`;
      const code =
        this.state === ServiceState.OPENING || this.state === ServiceState.INITIALIZING || this.state === ServiceState.STARTING
          ? ErrorCodes.INDEX_NOT_READY
          : ErrorCodes.BACKEND_FAILED;
      throw new BackendError(code, `the CodeGraph backend is not ready (${detail})`);
    }
    return this.backend;
  }

  /** Current tool schema for `name`: backend-derived when available, else static. */
  schemaFor(name) {
    if (this.schemaSource === 'backend') {
      const derived = deriveSchema(this.backendSchemas.get(name)?.inputSchema);
      if (derived) {
        return derived;
      }
    }
    return STATIC_TOOL_SCHEMAS[name] ?? { type: 'object', properties: {} };
  }

  sanitizeArguments(name, args) {
    if ('projectPath' in args) {
      throw new BackendError(
        ErrorCodes.INVALID_PARAMS,
        'projectPath is not accepted; queries are always scoped to this repository'
      );
    }
    const schema = this.schemaFor(name);
    const known = schema.properties ? Object.keys(schema.properties) : [];
    if (known.length > 0) {
      const unknown = Object.keys(args).filter((key) => !known.includes(key));
      if (unknown.length > 0) {
        throw new BackendError(ErrorCodes.INVALID_PARAMS, `unknown argument(s): ${unknown.sort().join(', ')}`);
      }
    }
    for (const filterKey of ['path', 'file']) {
      if (typeof args[filterKey] === 'string' && args[filterKey] !== '') {
        try {
          validateRelativeScope(args[filterKey]);
        } catch (error) {
          throw new BackendError(ErrorCodes.INVALID_PARAMS, `${filterKey}: ${error.message}`);
        }
      }
    }
    return { ...args };
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

// -- tool table -----------------------------------------------------------------

function makeTools(service) {
  const proxyHandler = (name) => async (args) => {
    const backend = service.requireBackend();
    const arguments_ = service.sanitizeArguments(name, args ?? {});
    const result = await backend.callTool(name, arguments_);
    const payload = {
      content: Array.isArray(result.content) ? result.content : [],
    };
    if (result.structuredContent !== undefined && result.structuredContent !== null) {
      payload.structuredContent = result.structuredContent;
    }
    if (result.isError) {
      payload.isError = true;
    }
    return payload;
  };

  const statusHandler = async () => {
    const payload = {
      tool: 'codegraph',
      wrapper_state: service.state,
      state_detail: service.stateDetail,
      index_dir_name: service.currentDirName(),
      allowlist: service.allowlist,
      schema_source: service.schemaSource,
    };
    if (service.lockError) {
      payload.error = service.lockError.toPayload();
      return toolText(payload);
    }
    if (service.backend && service.backend.alive()) {
      try {
        payload.backend = await service.backend.callTool('codegraph_status', {});
      } catch (error) {
        if (error instanceof BackendError) {
          payload.error = error.toPayload();
        } else {
          throw error;
        }
      }
    }
    return toolText(payload);
  };

  const handlers = new Map([['codegraph_status', statusHandler]]);

  // Tool names and handlers are fixed at startup; inputSchema refreshes from
  // the backend once it answers (see schemaFor).
  return () =>
    service.allowlist.map((name) => ({
      name,
      description:
        name === 'codegraph_status'
          ? 'Wrapper status: lifecycle state, index directory name, tool allowlist, backend health.'
          : service.schemaSource === 'backend' && service.backendSchemas.get(name)?.description
            ? service.backendSchemas.get(name).description
            : TOOL_DESCRIPTIONS[name] ?? `Wrapped CodeGraph tool ${name} (this repository only).`,
      inputSchema: service.schemaFor(name),
      annotations: name === 'codegraph_status' ? undefined : READ_ONLY_ANNOTATIONS,
      handler: handlers.get(name) ?? proxyHandler(name),
    }));
}

// -- entry point -----------------------------------------------------------------

function resolveBundle(paths, runtimeArg, cliArg) {
  let runtime = runtimeArg ?? null;
  let cliEntry = cliArg ?? null;
  let libraryDir = null;
  const prepared = new StateStore(paths.preparedStatePath()).read();
  if (prepared?.codegraph) {
    runtime = runtime ?? prepared.codegraph.runtime;
    cliEntry = cliEntry ?? prepared.codegraph.cli_entry;
    libraryDir = path.dirname(prepared.codegraph.library_entry);
  }
  if (!runtime || !cliEntry || !libraryDir) {
    process.stderr.write('codegraph-mcp: pinned CodeGraph bundle not found; run common/tools/prepare.mjs first\n');
    process.exit(2);
  }
  return { runtime: path.resolve(runtime), cliEntry: path.resolve(cliEntry), libraryDir: path.resolve(libraryDir) };
}

function main() {
  const { values } = parseArgs({
    options: {
      'repo-root': { type: 'string' },
      'build-dir': { type: 'string' },
      runtime: { type: 'string' },
      'cli-entry': { type: 'string' },
    },
  });

  const repoRoot = deriveRepoRoot(import.meta.url, values['repo-root']);
  const buildDir = resolveBuildDir(repoRoot, values['build-dir']);
  const paths = new WorkspacePaths(repoRoot, buildDir);
  paths.ensureDirs();

  const fakeScript = process.env.CODEGRAPH_MCP_FAKE_SCRIPT;
  let backendArgs;
  if (fakeScript) {
    backendArgs = { runtime: fakeScript, cliEntry: fakeScript, libraryDir: null, argvOverride: [process.execPath, path.resolve(fakeScript)] };
  } else {
    const bundle = resolveBundle(paths, values.runtime, values['cli-entry']);
    backendArgs = { ...bundle, argvOverride: null };
  }

  const service = new CodeGraphService(paths, backendArgs);
  void runWrapperServer({
    name: `${projectInfo(repoRoot).slug}-codegraph`,
    instructions: TOOL_INSTRUCTIONS,
    tools: makeTools(service),
    service,
  });
  return service;
}

main();
