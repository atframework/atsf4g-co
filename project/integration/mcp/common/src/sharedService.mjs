/** One cache owner per local user, workspace, tool and cache directory.
 * Kernel IPC binding elects the owner before any backend starts. Agent sockets
 * are lifelines; the last disconnect stops the backend before releasing IPC.
 * No TCP listener, stale-lock deletion, request replay or installed daemon.
 */
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import net from 'node:net';
import { createHash, createHmac, randomBytes, timingSafeEqual } from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';
import { StateStore, isPidAlive } from './state.mjs';
import { BackendError, ErrorCodes } from './errors.mjs';
import { LIMITS } from './limits.mjs';
import { currentRuntime, scriptInvocation } from './runtime.mjs';
import { nodeExecutables } from './localTools.mjs';

const WORKER = 'WORKSPACE_MCP_SHARED_WORKER';
const PROTOCOL = 1;
const START_MS = 15000;
const CALL_MS = 310000;
const MAX_CLIENTS = 64;
const MAX_PENDING = 64;
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const failure = message => new BackendError(ErrorCodes.BACKEND_FAILED, message);
const proof = (token, role, nonce) => createHmac('sha256', token).update(`${role}:${nonce}`).digest('hex');
const matches = (left, right) => typeof left === 'string' && Buffer.byteLength(left) === Buffer.byteLength(right)
  && timingSafeEqual(Buffer.from(left), Buffer.from(right));

export function sharedLocation(paths, tool) {
  const dir = paths.toolStateDir(tool);
  fs.mkdirSync(dir, { recursive: true });
  const canonical = fs.realpathSync(dir);
  const key = createHash('sha256').update(JSON.stringify([
    os.userInfo().username, process.platform === 'win32' ? canonical.toLowerCase() : canonical, tool,
  ])).digest('hex').slice(0, 32);
  // Abstract Linux sockets and Windows pipes are removed by the kernel, even
  // after SIGKILL. Never unlink a rendezvous endpoint while another can bind.
  const endpoint = process.platform === 'win32' ? `\\\\.\\pipe\\workspace-mcp-${key}`
    : process.platform === 'linux' ? `\0workspace-mcp-${key}` : path.join(dir, 'shared.sock');
  return { key, endpoint, record: path.join(paths.privateDir, `shared-${key}.json`),
    state: path.join(dir, 'shared-service.json'), log: path.join(dir, 'shared.stderr.log') };
}

function writePrivate(file, payload) {
  fs.mkdirSync(path.dirname(file), { recursive: true, mode: 0o700 });
  if (fs.lstatSync(path.dirname(file)).isSymbolicLink() || fs.lstatSync(file, { throwIfNoEntry: false })?.isSymbolicLink()) {
    throw new Error('shared service credentials must not be symlinks');
  }
  const temp = `${file}.${process.pid}.tmp`;
  try {
    fs.writeFileSync(temp, JSON.stringify(payload), { flag: 'wx', mode: 0o600 });
    fs.renameSync(temp, file);
  } finally { try { fs.unlinkSync(temp); } catch { /* renamed */ } }
}

/** Check the frame bound before retaining an unterminated line. */
export function readFrames(socket, onMessage, maxBytes = LIMITS.maxBackendFrameBytes) {
  let buffered = Buffer.alloc(0);
  socket.on('data', chunk => {
    let start = 0;
    for (;;) {
      const end = chunk.indexOf(10, start);
      const part = chunk.subarray(start, end < 0 ? chunk.length : end);
      if (buffered.length + part.length > maxBytes) return socket.destroy();
      buffered = Buffer.concat([buffered, part]);
      if (end < 0) return;
      let message;
      try { message = JSON.parse(buffered.toString('utf8')); } catch { return socket.destroy(); }
      buffered = Buffer.alloc(0);
      if (!message || typeof message !== 'object' || Array.isArray(message)) return socket.destroy();
      onMessage(message);
      if (socket.destroyed) return;
      start = end + 1;
    }
  });
}

function send(socket, message) {
  const frame = JSON.stringify(message) + '\n';
  if (Buffer.byteLength(frame) > LIMITS.maxBackendFrameBytes || socket.writableLength > LIMITS.maxBackendFrameBytes) {
    socket.destroy(); return;
  }
  if (!socket.destroyed) socket.write(frame);
}

function nodeRuntime(paths) {
  const prepared = new StateStore(paths.preparedStateReadPath()).read();
  for (const executable of [...new Set([prepared?.codegraph?.runtime, ...nodeExecutables()].filter(Boolean))]) {
    const result = spawnSync(executable, ['-p', 'JSON.stringify(process.versions)'], {
      encoding: 'utf8', shell: false, windowsHide: true, timeout: 5000,
    });
    try {
      const versions = JSON.parse(result.stdout);
      const [major, minor] = versions.node.split('.').map(Number);
      if (result.status === 0 && !versions.bun && !versions.deno && (major > 20 || major === 20 && minor >= 8)) {
        return { kind: 'node', executable, version: versions.node };
      }
    } catch { /* try next installed Node */ }
  }
  throw new Error('Shared MCP IPC needs an installed Node.js 20.8+ runtime (or the prepared CodeGraph runtime)');
}

/** Deno requires --allow-all for Windows pipes. Keep its existing permissions
 * and use a stdin-bound Node relay instead of granting that permission. */
export async function relayDeno(paths) {
  if (currentRuntime().kind !== 'deno') return false;
  const invocation = scriptInvocation(process.argv[1], process.argv.slice(2), nodeRuntime(paths));
  const child = spawn(invocation.command, invocation.args, {
    cwd: paths.repoRoot, env: process.env, shell: false, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'],
  });
  child.on('error', error => { process.stderr.write(`MCP relay: ${error.message}\n`); process.exit(1); });
  child.stdin.on('error', () => {});
  process.stdin.pipe(child.stdin); child.stdout.pipe(process.stdout); child.stderr.pipe(process.stderr);
  for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, () => child.stdin.end());
  process.stdin.on('error', () => child.stdin.end());
  child.on('exit', code => process.exit(code ?? 1));
  return true;
}

export async function serveShared({ location, service, getTools, callTool, tool, configuration }) {
  const peers = new Set();
  const clients = new Set();
  const token = randomBytes(32).toString('hex');
  let starting = null;
  let stopping = null;
  let queued = 0;
  let chain = Promise.resolve();
  let idleTimer;
  let stateWarning = false;
  const info = () => ({ protocol: PROTOCOL, pid: process.pid, clients: clients.size, tool,
    transport: process.platform === 'win32' ? 'named-pipe' : 'unix-socket' });
  const publish = (state = stopping ? 'stopping' : 'serving') => {
    try { new StateStore(location.state).write({ ...info(), state }); }
    catch (error) {
      // Diagnostics never own the process lifetime. In particular a Windows
      // rename sharing violation must not skip backend stop or lock release.
      if (!stateWarning) process.stderr.write(`shared status unavailable: ${error.message}\n`);
      stateWarning = true;
    }
  };
  const stop = () => {
    if (stopping) return stopping;
    stopping = (async () => {
      clearTimeout(idleTimer);
      publish('stopping');
      await starting;
      await service.shutdown('last-client-disconnected');
      publish('stopped');
      // IPC remains bound throughout backend cleanup; a new generation cannot
      // start a second writer while the old one is still shutting down.
      try { fs.unlinkSync(location.record); } catch { /* already removed */ }
      for (const peer of peers) peer.destroy();
      await new Promise(resolve => server.close(resolve));
      process.exit(0);
    })().catch(error => { process.stderr.write(`shared shutdown: ${error.message}\n`); process.exit(1); });
    return stopping;
  };
  const server = net.createServer(socket => {
    if (stopping || peers.size >= MAX_CLIENTS) return socket.destroy();
    peers.add(socket);
    let authenticated = false;
    let pending = false;
    const authTimer = setTimeout(() => socket.destroy(), 3000);
    socket.on('error', () => {});
    socket.on('close', () => {
      clearTimeout(authTimer); peers.delete(socket);
      if (clients.delete(socket)) {
        publish();
        if (!clients.size) void stop();
      }
    });
    readFrames(socket, message => {
      if (stopping) return socket.destroy();
      if (!authenticated) {
        if (message.protocol !== PROTOCOL || typeof message.nonce !== 'string' || message.nonce.length !== 64
          || !matches(message.proof, proof(token, 'client', message.nonce))) return socket.destroy();
        if (message.configuration !== configuration) {
          send(socket, { incompatible: true, proof: proof(token, 'server', message.nonce) });
          socket.end(); return;
        }
        authenticated = true; clients.add(socket); clearTimeout(authTimer); clearTimeout(idleTimer);
        if (!starting) starting = Promise.resolve().then(() => service.startup());
        publish(); send(socket, { ready: true, proof: proof(token, 'server', message.nonce), ...info() }); return;
      }
      if (message.method === 'detach') {
        clients.delete(socket); publish();
        if (!clients.size) void stop(); else socket.end();
        return;
      }
      if (!Number.isSafeInteger(message.id) || pending || queued >= MAX_PENDING) return socket.destroy();
      if (!['list', 'call'].includes(message.method)) return socket.destroy();
      pending = true; queued++;
      const run = async () => {
        try {
          await starting;
          if (socket.destroyed || stopping || !clients.has(socket)) return;
          let result;
          if (message.method === 'list') result = { tools: getTools().map(({ handler, ...metadata }) => metadata) };
          else {
            result = await callTool(message.name, message.args);
            if (message.name === `${tool}_status` && result.content?.[0]?.type === 'text') {
              const status = JSON.parse(result.content[0].text);
              result = { ...result, content: [{ type: 'text', text: JSON.stringify({ ...status, shared_service: info() }) }] };
            }
          }
          send(socket, { id: message.id, result });
        } catch (error) { send(socket, { id: message.id, error: error.message }); }
        finally { pending = false; queued--; }
      };
      chain = chain.then(run, run);
    }, LIMITS.maxRequestBytes);
  });
  try {
    await new Promise((resolve, reject) => {
      server.once('error', reject);
      server.listen({ path: location.endpoint, exclusive: true }, resolve);
    });
  } catch (error) {
    // Other candidates never touch the backend or shared state.
    if (error.code === 'EADDRINUSE') { process.exit(0); return; }
    throw error;
  }
  server.on('error', () => void stop());
  writePrivate(location.record, { token, protocol: PROTOCOL });
  publish();
  idleTimer = setTimeout(() => void stop(), START_MS);
  for (const signal of ['SIGTERM', 'SIGINT']) process.on(signal, () => void stop());
}

export class SharedClient {
  constructor(paths, tool, configuration) {
    this.paths = paths; this.location = sharedLocation(paths, tool);
    this.configuration = configuration;
    this.socket = null; this.connecting = null; this.closed = false;
    this.pending = null; this.nextId = 1; this.chain = Promise.resolve(); this.queued = 0;
  }

  spawnOwner() {
    const invocation = scriptInvocation(process.argv[1], process.argv.slice(2), nodeRuntime(this.paths));
    const log = fs.openSync(this.location.log, 'a');
    try {
      const child = spawn(invocation.command, invocation.args, { cwd: this.paths.repoRoot,
        env: { ...process.env, [WORKER]: '1' }, shell: false, windowsHide: true,
        detached: true, stdio: ['ignore', 'ignore', log] });
      child.on('error', () => {}); child.unref();
      return child;
    } finally { fs.closeSync(log); }
  }

  async connect() {
    if (this.closed) throw failure('shared client is closed');
    if (this.socket && !this.socket.destroyed) return;
    if (this.connecting) return this.connecting;
    this.connecting = (async () => {
      const deadline = Date.now() + START_MS;
      let candidate = null;
      let lastSpawn = 0;
      while (!this.closed && Date.now() < deadline) {
        try {
          const credentials = JSON.parse(fs.readFileSync(this.location.record, 'utf8'));
          await this.attach(credentials);
          // A slow election loser can otherwise bind after the winning owner
          // has already stopped and hold a workspace open with no clients.
          if (candidate?.pid && candidate.pid !== this.ownerPid && candidate.exitCode === null) {
            await new Promise(resolve => {
              candidate.once('exit', resolve);
              if (!candidate.kill()) resolve();
            });
          }
          return;
        } catch (error) {
          if (error.code === 'SHARED_CONFIG_MISMATCH') throw error;
          // A PID is a conservative liveness hint, never an ownership grant or
          // substitute for authenticated IPC. An unreachable live owner must
          // not cause an independent backend to be started.
          const owner = new StateStore(this.location.state).read();
          const ownerMayBeAlive = owner?.state === 'serving' && isPidAlive(owner.pid);
          if (!ownerMayBeAlive && (!candidate || candidate.exitCode !== null) && Date.now() - lastSpawn >= 1000) {
            candidate = this.spawnOwner(); lastSpawn = Date.now();
          }
          await delay(50);
        }
      }
      throw failure('shared IPC unavailable; see shared.stderr.log and shared-service.json; a live PID cannot replace IPC (no independent backend started, no query replayed)');
    })().finally(() => { this.connecting = null; });
    return this.connecting;
  }

  attach(credentials) {
    if (credentials?.protocol !== PROTOCOL || typeof credentials.token !== 'string' || !/^[a-f0-9]{64}$/.test(credentials.token)) {
      return Promise.reject(failure('shared service authentication record is invalid'));
    }
    return new Promise((resolve, reject) => {
      const socket = net.connect(this.location.endpoint);
      const nonce = randomBytes(32).toString('hex');
      let ready = false;
      const timer = setTimeout(() => socket.destroy(), 1000);
      socket.on('connect', () => send(socket, { protocol: PROTOCOL, nonce,
        configuration: this.configuration, proof: proof(credentials.token, 'client', nonce) }));
      socket.on('error', () => {});
      socket.on('close', () => {
        clearTimeout(timer);
        if (this.socket === socket) {
          this.socket = null;
          this.pending?.reject(failure('shared service connection closed; request was not replayed'));
          this.pending = null;
        }
        if (!ready) reject(failure('shared service did not authenticate'));
      });
      readFrames(socket, message => {
        if (!ready) {
          if (message.incompatible && matches(message.proof, proof(credentials.token, 'server', nonce))) {
            const error = failure('shared backend configuration changed; close all Agents using this cache and reconnect');
            error.code = 'SHARED_CONFIG_MISMATCH'; reject(error); socket.destroy(); return;
          }
          if (!message.ready || message.protocol !== PROTOCOL || this.closed
            || !matches(message.proof, proof(credentials.token, 'server', nonce))) return socket.destroy();
          ready = true; clearTimeout(timer); this.socket = socket; this.ownerPid = message.pid; resolve(); return;
        }
        if (message.id !== this.pending?.id) return socket.destroy();
        const pending = this.pending; this.pending = null;
        if (message.error) pending.reject(failure(message.error)); else pending.resolve(message.result);
      });
    });
  }

  request(method, params = {}) {
    if (this.queued >= MAX_PENDING) return Promise.reject(failure('shared request queue is full'));
    this.queued++;
    const run = async () => {
      await this.connect();
      const id = this.nextId++;
      if (Buffer.byteLength(JSON.stringify(params)) > LIMITS.maxRequestBytes) throw new BackendError(ErrorCodes.REQUEST_TOO_LARGE, 'request exceeds size limit');
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => { this.socket?.destroy(); reject(failure('shared request timed out; request was not replayed')); }, CALL_MS);
        this.pending = { id, resolve: value => { clearTimeout(timer); resolve(value); }, reject: error => { clearTimeout(timer); reject(error); } };
        send(this.socket, { id, method, ...params });
      });
    };
    const result = this.chain.then(run, run).finally(() => { this.queued--; });
    this.chain = result.catch(() => {});
    return result;
  }

  async shutdown() {
    this.closed = true;
    await this.connecting?.catch(() => {});
    const socket = this.socket;
    if (!socket || socket.destroyed) return;
    await new Promise(resolve => {
      const timer = setTimeout(() => { socket.destroy(); resolve(); }, 16000);
      socket.once('close', () => { clearTimeout(timer); resolve(); });
      send(socket, { method: 'detach' });
    });
  }
}

export function isSharedWorker() { return process.env[WORKER] === '1'; }

/** A different binary, tool allowlist or LLM configuration cannot silently
 * attach to an existing service. The hash carries no credential contents. */
export function configurationIdentity(service, tool) {
  const paths = service.paths;
  const prepared = new StateStore(paths.preparedStateReadPath()).read();
  const credentialFile = tool === 'sirchmunk' ? paths.readPath('private/sirchmunk.json') : null;
  return createHash('sha256').update(JSON.stringify({
    protocol: PROTOCOL, tool, backend: service.backendArgs ?? prepared?.[tool], allowlist: service.allowlist,
    credentials: tool === 'sirchmunk' && fs.existsSync(credentialFile) ? fs.readFileSync(credentialFile, 'utf8') : null,
  }, (_key, value) => {
    if (typeof value !== 'string' || !path.isAbsolute(value) || !fs.existsSync(value)) return value;
    const canonical = fs.realpathSync(value);
    return process.platform === 'win32' ? canonical.toLowerCase() : canonical;
  })).digest('hex');
}
