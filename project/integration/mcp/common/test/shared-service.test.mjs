import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import net from 'node:net';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { WorkspacePaths, deriveRepoRoot } from '../src/paths.mjs';
import { isPidAlive, ToolInstanceLock, currentIdentity } from '../src/state.mjs';
import { sharedLocation, SharedClient } from '../src/sharedService.mjs';
import { loadSdk } from '../src/sdk.mjs';
import { writeConfig } from '../../tools/sirchmunk/src/config.mjs';
import { windowsDescendants } from './windows-processes.mjs';
const { Client } = await loadSdk('tools/codegraph', '@modelcontextprotocol/client');
const { StdioClientTransport } = await loadSdk('tools/codegraph', '@modelcontextprotocol/client/stdio');
const toolkit = fileURLToPath(new URL('../../', import.meta.url));
const root = deriveRepoRoot(import.meta.url);
const scratch = path.join(new WorkspacePaths(root).agentTmpDir, 'shared-tests');
fs.mkdirSync(scratch, { recursive: true });
const text = result => JSON.parse(result.content[0].text);
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

async function until(check, message) {
  for (const end = Date.now() + 12000; Date.now() < end;) {
    if (await check()) return;
    await sleep(30);
  }
  assert.fail(message);
}

async function fixture(t, tool) {
  const repo = fs.mkdtempSync(path.join(scratch, tool + '-'));
  const paths = new WorkspacePaths(repo, path.join(repo, 'build'));
  paths.ensureDirs();
  const starts = path.join(repo, 'starts.log');
  const clients = [];
  const ownedProcesses = new Set();
  const capturedReadyOwners = new Set();
  const backends = () => fs.existsSync(starts) ? fs.readFileSync(starts, 'utf8').trim().split('\n').map(Number) : [];
  if (tool === 'sirchmunk') {
    writeConfig(paths, { baseURL: 'https://llm.example.test/v1', apiKey: 'unused-fixture-key', model: 'fixture' });
    fs.writeFileSync(paths.preparedStatePath(), JSON.stringify({ sirchmunk: {
      python: 'fake', model_dir: path.join(paths.downloadsDir, 'models/sirchmunk'),
    } }));
  }
  t.after(async () => {
    await Promise.all(clients.map(({ client }) => client.close()));
    await until(() => clients.every(entry => !isPidAlive(entry.pid)), 'all Agent wrapper processes must exit');
    await until(() => backends().every(pid => !isPidAlive(pid)), 'all owned backends must exit');
    await until(() => [...ownedProcesses].every(pid => !isPidAlive(pid)), 'shared owners and console hosts must exit before fixture removal');
    // Keep the event loop running while child stdio and Windows directory
    // handles finish closing; synchronous removal blocks those close events.
    await fs.promises.rm(repo, { recursive: true, force: true });
  });
  const launch = async (extraEnv = {}, legacyBuild = paths.buildDir) => {
    const fake = { tgrep: 'fake-tgrep-serve.mjs', codegraph: 'fake-codegraph.mjs', sirchmunk: 'fake-backend.mjs' }[tool];
    const transport = new StdioClientTransport({ command: process.execPath,
      args: [path.join(toolkit, 'tools', tool, 'src/server.mjs'), '--repo-root', process.platform === 'win32' && clients.length === 1 ? repo.toUpperCase() : repo, '--build-dir', legacyBuild],
      env: { ...process.env, [`${tool.toUpperCase()}_MCP_FAKE_SCRIPT`]: path.join(toolkit, 'tools', tool, 'test', fake), MCP_TEST_START_LOG: starts, ...extraEnv },
      stderr: 'pipe' });
    transport.stderr?.on('data', () => {});
    const client = new Client({ name: 'shared-agent', version: '1' });
    const entry = { client, transport }; clients.push(entry);
    await client.connect(transport);
    entry.pid = transport.pid;
    return entry;
  };
  const status = async client => {
    const result = await client.callTool({ name: `${tool}_status`, arguments: {} });
    assert.notEqual(result.isError, true, JSON.stringify(result));
    const payload = text(result);
    const owner = payload.shared_service?.pid;
    if (owner) ownedProcesses.add(owner);
    if (owner && (payload.wrapper_state ?? payload.state) === 'ready' && !capturedReadyOwners.has(owner)) {
      capturedReadyOwners.add(owner);
      for (const pid of windowsDescendants(owner)) ownedProcesses.add(pid);
    }
    return payload;
  };
  const ready = async client => {
    let found;
    await until(async () => { found = await status(client); return (found.wrapper_state ?? found.state) === 'ready'; }, `${tool} must become ready`);
    return found;
  };
  const query = async (client, word) => {
    const request = tool === 'tgrep' ? { name: 'tgrep_search', arguments: { pattern: word } }
      : tool === 'codegraph' ? { name: 'codegraph_explore', arguments: { query: word } }
        : { name: 'sirchmunk_search', arguments: { query: word, mode: 'FILENAME_ONLY' } };
    const result = await client.callTool(request);
    assert.notEqual(result.isError, true, JSON.stringify(result));
    if (tool === 'codegraph') assert.equal(result.structuredContent.echoed_query, word);
    else if (tool === 'sirchmunk') assert.equal(text(result).query, word);
    else assert.equal(text(result).num_matches, 2);
  };
  return { paths, launch, ready, status, query, backends };
}

for (const tool of ['tgrep', 'codegraph', 'sirchmunk']) {
  test(`${tool}: simultaneous Agents share one backend, isolated replies and disconnect ownership`, async t => {
    const f = await fixture(t, tool);
    const agents = await Promise.all(['build-a', 'build-b', 'build-c'].map(dir => f.launch({}, path.join(f.paths.repoRoot, dir))));
    const states = await Promise.all(agents.map(agent => f.ready(agent.client)));
    assert.equal(new Set(states.map(state => state.shared_service.pid)).size, 1);
    assert.equal(f.backends().length, 1, 'only one backend process may start');
    assert.equal((await f.status(agents[0].client)).shared_service.clients, 3);
    await Promise.all(agents.map((agent, i) => f.query(agent.client, 'distinct-query-' + i)));
    const workerPid = states[0].shared_service.pid;
    process.kill(agents[0].transport.pid, 'SIGKILL');
    await until(async () => (await f.status(agents[1].client)).shared_service.clients === 2, 'forced Agent exit releases its lease');
    await agents[1].client.close();
    const remaining = await f.status(agents[2].client);
    assert.equal(remaining.shared_service.pid, workerPid);
    assert.equal(remaining.shared_service.clients, 1);
    assert.equal(f.backends().length, 1, 'closing clients must not restart the shared cache');
    await f.query(agents[2].client, 'survivor');
    process.kill(agents[2].transport.pid, 'SIGKILL');
    await until(() => !isPidAlive(workerPid) && f.backends().every(pid => !isPidAlive(pid)), 'last forced Agent exit cleans worker and backend');
    assert.equal(fs.existsSync(path.join(f.paths.toolStateDir(tool), `${tool}.instance.lock`)), false);
  });

  test(`${tool}: worker crash ends its backend; next request reconnects without a second writer`, async t => {
    const f = await fixture(t, tool);
    const agents = await Promise.all([f.launch(), f.launch()]);
    const previous = await f.ready(agents[0].client);
    await f.ready(agents[1].client);
    const oldPid = f.backends()[0];
    process.kill(previous.shared_service.pid, 'SIGKILL');
    await until(() => !isPidAlive(oldPid), 'backend must observe worker stdin EOF');
    const recovered = await Promise.all(agents.map(agent => f.ready(agent.client)));
    assert.equal(new Set(recovered.map(state => state.shared_service.pid)).size, 1);
    assert.notEqual(recovered[0].shared_service.pid, previous.shared_service.pid);
    assert.equal(f.backends().length, 2, 'one replacement backend must serve both surviving Agents');
    await Promise.all(agents.map(agent => f.query(agent.client, 'after-recovery')));
  });
}

test('unauthenticated or oversized IPC cannot query or retain a cache service', async t => {
  const f = await fixture(t, 'tgrep');
  const agent = await f.launch();
  const before = await f.ready(agent.client);
  const location = sharedLocation(f.paths, 'tgrep');
  for (const frame of [JSON.stringify({ id: 1, method: 'call', name: 'tgrep_search' }) + '\n', 'x'.repeat(8 * 1024 * 1024 + 1)]) {
    const socket = net.connect(location.endpoint);
    socket.on('error', () => {});
    await new Promise(resolve => { socket.on('connect', () => socket.write(frame)); socket.on('close', resolve); });
    assert.equal((await f.status(agent.client)).shared_service.clients, 1);
  }
  await agent.client.close();
  await until(() => !isPidAlive(before.shared_service.pid), 'unauthenticated sockets must not retain the service');
});

test('diagnostic write failure cannot prevent last-client backend cleanup and lock release', async t => {
  const f = await fixture(t, 'sirchmunk');
  const agent = await f.launch();
  const state = await f.ready(agent.client);
  for (const name of ['shared-service.json', 'wrapper-state.json']) {
    const file = path.join(f.paths.toolStateDir('sirchmunk'), name);
    fs.rmSync(file);
    fs.mkdirSync(file); // deterministic rename failure, including as an administrator
  }
  await agent.client.close();
  await until(() => !isPidAlive(state.shared_service.pid), 'shared process must stop despite failed status writes');
  assert.equal(f.backends().length, 1);
  assert.equal(fs.existsSync(path.join(f.paths.toolStateDir('sirchmunk'), 'sirchmunk.instance.lock')), false);
});

test('a worker crash fails an in-flight request without replaying it after reconnect', async t => {
  const f = await fixture(t, 'tgrep');
  const receipt = path.join(f.paths.repoRoot, 'received.txt');
  const agent = await f.launch({ TGREP_FAKE_SEARCH_RECEIPT: receipt, TGREP_FAKE_HOLD_SEARCH: '1' });
  const previous = await f.ready(agent.client);
  const response = agent.client.callTool({ name: 'tgrep_search', arguments: { pattern: 'execute-once' } });
  await until(() => fs.existsSync(receipt), 'backend must receive request before crash');
  process.kill(previous.shared_service.pid, 'SIGKILL');
  const failed = await response;
  assert.equal(failed.isError, true);
  assert.equal(text(failed).code, 'BACKEND_FAILED');
  await until(() => !isPidAlive(f.backends()[0]), 'old backend must close');
  await f.ready(agent.client);
  assert.equal(f.backends().length, 2);
  assert.equal(fs.readFileSync(receipt, 'utf8'), 'execute-once\n');
});

test('different backend configuration is rejected without disturbing existing clients', async t => {
  const f = await fixture(t, 'codegraph');
  const first = await f.launch();
  const before = await f.ready(first.client);
  fs.writeFileSync(path.join(f.paths.toolStateDir('codegraph'), 'extra-tools.json'), JSON.stringify({ enabled_tools: ['codegraph_callers'] }));
  const second = await f.launch();
  const result = await second.client.callTool({ name: 'codegraph_status', arguments: {} });
  assert.equal(result.isError, true);
  assert.equal(text(result).code, 'SHARED_CONFIG_MISMATCH');
  assert.equal((await f.status(first.client)).shared_service.pid, before.shared_service.pid);
  await f.query(first.client, 'still-usable');
  assert.equal(f.backends().length, 1);
});

test('a live legacy instance is not stolen and its diagnostic state is preserved', async t => {
  const f = await fixture(t, 'tgrep');
  const dir = f.paths.toolStateDir('tgrep');
  const lock = new ToolInstanceLock(dir, 'tgrep');
  lock.acquire(currentIdentity(), f.paths.repoRoot, 'ready');
  const stateFile = path.join(dir, 'wrapper-state.json');
  fs.writeFileSync(stateFile, '{"legacy_owner":"unchanged"}');
  try {
    const agent = await f.launch();
    const status = await f.status(agent.client);
    assert.equal(status.error.code, 'INDEX_IN_USE');
    assert.equal(fs.readFileSync(stateFile, 'utf8'), '{"legacy_owner":"unchanged"}');
    assert.equal(f.backends().length, 0);
    await agent.client.close();
    assert.equal(lock.readHolder().identity.pid, process.pid);
  } finally { lock.release(); }
});

test('an unreachable IPC owner with a live PID never starts another backend', async t => {
  const f = await fixture(t, 'tgrep');
  const location = sharedLocation(f.paths, 'tgrep');
  fs.writeFileSync(location.state, JSON.stringify({ state: 'serving', pid: process.pid }));
  const client = new SharedClient(f.paths, 'tgrep', 'fixture');
  let starts = 0;
  client.spawnOwner = () => { starts++; throw new Error('must not spawn over a live owner'); };
  await assert.rejects(client.connect(), /live PID cannot replace IPC/);
  await client.shutdown();
  assert.equal(starts, 0);
  assert.equal(f.backends().length, 0);
});

test('switching data directories refuses a live legacy cache owner without touching it', async t => {
  const f = await fixture(t, 'tgrep');
  const legacy = path.join(f.paths.buildDir, 'integration/mcp/state/tgrep', path.basename(path.dirname(f.paths.toolStateDir('tgrep'))), path.basename(f.paths.toolStateDir('tgrep')));
  const lock = new ToolInstanceLock(legacy, 'tgrep');
  lock.acquire(currentIdentity(), f.paths.repoRoot, 'ready');
  try {
    const bytes = fs.readFileSync(lock.path);
    const agent = await f.launch();
    assert.equal((await f.status(agent.client)).error.code, 'INDEX_IN_USE');
    assert.equal(f.backends().length, 0);
    assert.deepEqual(fs.readFileSync(lock.path), bytes);
    await agent.client.close();
  } finally { lock.release(); }
});
