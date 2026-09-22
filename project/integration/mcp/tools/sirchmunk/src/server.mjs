#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import { WorkspacePaths, deriveRepoRoot, resolveBuildDir, validateWorkspaceBuildDir, validateRelativeScope } from '../../../common/src/paths.mjs';
import { runWrapperServer, toolText } from '../../../common/src/mcpServer.mjs';
import { BackendError, ErrorCodes } from '../../../common/src/errors.mjs';
import { ToolInstanceLock, currentIdentity, StateStore } from '../../../common/src/state.mjs';
import { supervise, minimalEnvironment, StderrSink } from '../../../common/src/supervisor.mjs';
import { LineJsonRpcClient } from '../../../common/src/lineRpc.mjs';
import { scriptInvocation } from '../../../common/src/runtime.mjs';
import { configPath, readConfig } from './config.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));

export class SirchmunkService {
  constructor(paths) {
    this.paths = paths;
    this.lock = new ToolInstanceLock(paths.toolStateDir('sirchmunk'), 'sirchmunk');
    this.store = new StateStore(path.join(paths.toolStateDir('sirchmunk'), 'wrapper-state.json'));
    this.state = 'starting';
    this.detail = '';
    this.proc = null;
    this.rpc = null;
    this.timer = null;
    this.backendStatus = null;
    this.failure = null;
  }

  publish() { if (this.lock.locked) this.store.write({ ...this.backendStatus, state: this.state, state_detail: this.detail }); }

  startup() {
    try {
      this.lock.acquire(currentIdentity(), this.paths.repoRoot, 'starting');
      const prepared = JSON.parse(fs.readFileSync(this.paths.preparedStatePath(), 'utf8')).sirchmunk;
      if (!prepared?.python) throw new Error('Sirchmunk is not prepared; run setup.js --backend=sirchmunk');
      if (!readConfig(this.paths)) throw new Error('Sirchmunk LLM settings are missing; rerun setup.js');
      const work = path.join(this.paths.toolStateDir('sirchmunk'), 'work');
      validateWorkspaceBuildDir(this.paths.repoRoot, work);
      validateWorkspaceBuildDir(this.paths.repoRoot, prepared.model_dir);
      fs.mkdirSync(work, { recursive: true });
      const fake = process.env.SIRCHMUNK_MCP_FAKE_SCRIPT;
      const invoke = fake && scriptInvocation(path.resolve(fake));
      const argv = invoke ? [invoke.command, ...invoke.args] : [prepared.python, '-I', path.join(HERE, '../python/bridge.py'),
        configPath(this.paths), this.paths.repoRoot, work, prepared.model_dir, ...(prepared.offline ? ['--offline'] : [])];
      const dirs = [...new Set(Object.values(prepared.binaries ?? {}).map(file => path.dirname(file)))];
      const env = invoke ? { ...process.env } : minimalEnvironment({
        PATH: [...dirs, process.env.PATH ?? ''].join(path.delimiter), PYTHONUTF8: '1', PYTHONNOUSERSITE: '1',
        PYTHONDONTWRITEBYTECODE: '1', SIRCHMUNK_WORK_PATH: work, MCP_TRANSPORT: 'stdio',
        EMBEDDING_CACHE_DIR: prepared.model_dir, HF_HUB_DISABLE_TELEMETRY: '1', DO_NOT_TRACK: '1',
      });
      this.proc = supervise({ argv, cwd: this.paths.repoRoot, env, label: 'sirchmunk',
        stderrSink: new StderrSink({ filePath: path.join(this.paths.toolStateDir('sirchmunk'), 'backend.stderr.log') }) });
      this.rpc = new LineJsonRpcClient(this.proc);
      this.proc.exited.then(() => {
        if (this.state !== 'stopped') { this.state = 'failed'; this.detail = 'Sirchmunk backend exited; see backend.stderr.log'; this.publish(); }
      });
      void this.poll();
    } catch (error) {
      this.failure = error;
      this.state = 'failed'; this.detail = error.message; this.publish();
    }
  }

  async poll() {
    try {
      this.backendStatus = await this.rpc.request('status', {}, 300000);
      if (this.state === 'stopped') return;
      this.state = this.backendStatus.state;
      this.publish();
    } catch (error) {
      if (this.state === 'stopped') return;
      this.state = 'failed'; this.detail = error.message; this.publish();
      return;
    }
    this.timer = setTimeout(() => void this.poll(), 2000);
    this.timer.unref();
  }

  status() { return { ...this.backendStatus, state: this.state, detail: this.detail, enable_knowledge_evolution: this.backendStatus?.enable_knowledge_evolution ?? false }; }

  async call(method, args) {
    if (this.failure instanceof BackendError) throw this.failure;
    if (this.state !== 'ready') throw new BackendError(ErrorCodes.INDEX_NOT_READY, 'Sirchmunk is not ready; call sirchmunk_status', this.status());
    const fields = { search: ['query', 'mode', 'paths'], get_cluster: ['cluster_id'], list_clusters: ['limit'] }[method];
    if (!fields || Object.keys(args).some(key => !fields.includes(key))) throw new BackendError(ErrorCodes.INVALID_PARAMS, 'unknown search parameter');
    if (method === 'search') {
      if (typeof args.query !== 'string' || !args.query.trim() || args.query.length > 8192) throw new BackendError(ErrorCodes.INVALID_PARAMS, 'query must contain 1..8192 characters');
      if (args.paths !== undefined && (!Array.isArray(args.paths) || !args.paths.length || args.paths.length > 16 || args.paths.some(scope => typeof scope !== 'string'))) throw new BackendError(ErrorCodes.INVALID_PARAMS, 'paths must be a list of workspace-relative paths');
      for (const scope of args.paths ?? []) { if (scope !== '.') validateRelativeScope(scope); }
    }
    return this.rpc.request(method, args, 300000);
  }

  async shutdown(reason) {
    this.state = 'stopped'; this.detail = reason;
    clearTimeout(this.timer);
    let deadline;
    try {
      if (this.rpc && !this.rpc.closed) await Promise.race([
        this.rpc.request('shutdown', {}, 1000),
        new Promise(resolve => { deadline = setTimeout(resolve, 1000); }),
      ]);
    } catch { /* busy or failed backend; EOF still closes the process tree */ }
    finally { clearTimeout(deadline); }
    this.rpc?.close();
    if (this.proc) await this.proc.stop();
    this.publish(); this.lock.release();
  }
}

function main() {
  const { values } = parseArgs({ options: { 'repo-root': { type: 'string' }, 'build-dir': { type: 'string' } } });
  const root = deriveRepoRoot(import.meta.url, values['repo-root']);
  const paths = new WorkspacePaths(root, validateWorkspaceBuildDir(root, resolveBuildDir(root, values['build-dir'])));
  const service = new SirchmunkService(paths);
  const tool = (name, method, description, properties, required = []) => ({ name, description,
    inputSchema: { type: 'object', properties, required, additionalProperties: false },
    handler: async args => toolText(await service.call(method, args)) });
  void runWrapperServer({ name: 'workspace-sirchmunk',
    instructions: 'Search only this workspace. FAST and DEEP send relevant content to the configured LLM. Check sirchmunk_status for embedding download and knowledge evolution readiness.', service,
    tools: [
      { name: 'sirchmunk_status', description: 'Check backend, embedding and knowledge evolution status.', inputSchema: { type: 'object', properties: {}, additionalProperties: false }, handler: () => toolText(service.status()) },
      tool('sirchmunk_search', 'search', 'Search workspace documents and source. FAST/DEEP use the configured LLM; FILENAME_ONLY finds names. Paths are relative to this workspace.',
        { query: { type: 'string' }, mode: { type: 'string', enum: ['FAST', 'DEEP', 'FILENAME_ONLY'], default: 'FAST' }, paths: { type: 'array', items: { type: 'string' }, maxItems: 16 } }, ['query']),
      tool('sirchmunk_get_cluster', 'get_cluster', 'Read one knowledge cluster created in this workspace.', { cluster_id: { type: 'string' } }, ['cluster_id']),
      tool('sirchmunk_list_clusters', 'list_clusters', 'List knowledge clusters in this workspace.', { limit: { type: 'integer', minimum: 1, maximum: 100, default: 10 } }),
    ] });
}

main();
