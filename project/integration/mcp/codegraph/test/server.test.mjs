import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const SERVER = path.join(HERE, '..', 'src', 'server.mjs');
const FAKE = path.join(HERE, 'fake-codegraph.mjs');
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..', '..');

test('closing MCP during the first index cancels the helper and releases the session', async () => {
  const { WorkspacePaths } = await import('../../common/src/paths.mjs');
  const root = tmpBuildDir();
  const paths = new WorkspacePaths(root, path.join(root, 'build'));
  paths.ensureDirs();
  const pidFile = path.join(root, 'initializer.pid');
  const cli = path.join(root, 'cli.js');
  fs.writeFileSync(path.join(root, 'package.json'), '{"type":"module"}');
  fs.writeFileSync(cli, 'throw new Error("backend must never start after cancellation");');
  fs.writeFileSync(path.join(root, 'index.js'), `
    import fs from 'node:fs';
    export class CodeGraph {
      static isInitialized() { return false; }
      static async init() {
        const file = ${JSON.stringify(pidFile)};
        fs.writeFileSync(file + '.tmp', String(process.pid));
        fs.renameSync(file + '.tmp', file);
        return new Promise(() => setInterval(() => {}, 1000));
      }
    }
  `);
  fs.writeFileSync(paths.preparedStatePath(), JSON.stringify({ codegraph: {
    runtime: process.execPath, cli_entry: cli, library_entry: path.join(root, 'index.js'), acquisition: 'local',
  } }));
  const transport = new StdioClientTransport({ command: process.execPath,
    args: [SERVER, '--repo-root', root, '--build-dir', paths.buildDir], cwd: root,
    env: { SYSTEMROOT: process.env.SYSTEMROOT, TEMP: process.env.TEMP, TMP: process.env.TMP } });
  const client = new Client({ name: 'cancel-index-test', version: '1.0.0' });
  try {
    await client.connect(transport);
    for (const end = Date.now() + 10000; !fs.existsSync(pidFile) && Date.now() < end;) await new Promise(resolve => setTimeout(resolve, 25));
    assert.ok(fs.existsSync(pidFile), 'first-index helper reached the controlled library');
    const childPid = Number(fs.readFileSync(pidFile, 'utf8'));
    assert.ok(Number.isInteger(childPid) && childPid > 0, 'ready record must contain the actual child PID');
    const wrapperPid = transport.pid;
    const status = textOf(await client.callTool({ name: 'codegraph_status', arguments: {} }));
    assert.equal(status.wrapper_state, 'initializing');
    await client.close();
    assert.ok(await waitForPidExit(wrapperPid));
    assert.ok(await waitForPidExit(childPid));
    const state = JSON.parse(fs.readFileSync(path.join(paths.toolStateDir('codegraph'), 'wrapper-state.json')));
    assert.equal(state.state, 'stopped');
    assert.equal(state.auto_sync, null);
    assert.ok(!fs.existsSync(path.join(paths.toolStateDir('codegraph'), 'backend.stderr.log')));
  } finally {
    await client.close();
    fs.rmSync(root, { recursive: true, force: true });
  }
});

function tmpBuildDir() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-cg-server-'));
}

async function launchServer(envExtra = {}) {
  const buildDir = tmpBuildDir();
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [SERVER, '--repo-root', REPO_ROOT, '--build-dir', buildDir],
    env: {
      ...envExtra,
      SYSTEMROOT: process.env.SYSTEMROOT,
      TEMP: process.env.TEMP,
      TMP: process.env.TMP,
    },
    cwd: REPO_ROOT,
  });
  const client = new Client({ name: 'cg-server-test', version: '0.0.1' });
  await client.connect(transport);
  return { client, transport, buildDir };
}

function textOf(result) {
  assert.equal(result.content[0].type, 'text');
  return JSON.parse(result.content[0].text);
}

async function waitForPidExit(pid, timeoutMs = 10_000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      process.kill(pid, 0);
    } catch {
      return true;
    }
    await new Promise((resolve) => {
      setTimeout(resolve, 100);
    });
  }
  return false;
}

async function waitReady(client, timeoutMs = 15_000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const status = textOf(await client.callTool({ name: 'codegraph_status', arguments: {} }));
    if (status.wrapper_state === 'ready') {
      return status;
    }
    await new Promise((resolve) => {
      setTimeout(resolve, 200);
    });
  }
  throw new Error('wrapper did not reach ready in time');
}

test('default allowlist is explore + wrapped status, schema has no projectPath', async () => {
  const { client, transport, buildDir } = await launchServer({ CODEGRAPH_MCP_FAKE_SCRIPT: FAKE });
  try {
    await waitReady(client);
    const list = await client.listTools();
    assert.deepEqual(
      list.tools.map((tool) => tool.name).sort(),
      ['codegraph_explore', 'codegraph_status']
    );
    for (const tool of list.tools) {
      assert.equal(tool.inputSchema.properties?.projectPath, undefined, `${tool.name} must not expose projectPath`);
      assert.equal((tool.inputSchema.required ?? []).includes('projectPath'), false);
    }
  } finally {
    await client.close();
    assert.ok(await waitForPidExit(transport.pid));
    fs.rmSync(buildDir, { recursive: true, force: true });
  }
});

test('explore is proxied; projectPath and unknown arguments are rejected', async () => {
  const recordFile = path.join(os.tmpdir(), `mcp-cg-record-${Date.now()}.jsonl`);
  const { client, transport, buildDir } = await launchServer({
    CODEGRAPH_MCP_FAKE_SCRIPT: FAKE,
    CODEGRAPH_FAKE_RECORD: recordFile,
  });
  try {
    await waitReady(client);

    const result = await client.callTool({ name: 'codegraph_explore', arguments: { query: 'team manager', maxFiles: 3 } });
    assert.equal(result.isError, undefined);
    assert.equal(result.structuredContent.echoed_query, 'team manager');

    const escaped = await client.callTool({
      name: 'codegraph_explore',
      arguments: { query: 'x', projectPath: 'C:\\other\\project' },
    });
    assert.equal(escaped.isError, true);
    assert.equal(textOf(escaped).code, 'INVALID_PARAMS');

    const unknown = await client.callTool({ name: 'codegraph_explore', arguments: { query: 'x', evilFlag: true } });
    assert.equal(unknown.isError, true);
    assert.equal(textOf(unknown).code, 'INVALID_PARAMS');

    // Only the sanitized explore call reached the backend (status polling is
    // a separate tool and not counted here).
    const recorded = fs.readFileSync(recordFile, 'utf8').trim().split('\n').map((line) => JSON.parse(line));
    const explores = recorded.filter((entry) => entry.name === 'codegraph_explore');
    assert.equal(explores.length, 1);
    assert.deepEqual(explores[0].args, { query: 'team manager', maxFiles: 3 });
  } finally {
    await client.close();
    await waitForPidExit(transport.pid);
    fs.rmSync(buildDir, { recursive: true, force: true });
    fs.rmSync(recordFile, { force: true });
  }
});

test('extra-tools config extends the allowlist; dispatch re-checks it', async () => {
  const buildDir = tmpBuildDir();
  const extraConfig = path.join(buildDir, 'integration', 'mcp', 'state', 'codegraph');
  // workspaceId is derived from the repo root; write through the same helper the server uses.
  const { workspaceId, platformName } = await import('../../common/src/paths.mjs');
  const toolDir = path.join(extraConfig, workspaceId(REPO_ROOT), platformName());
  fs.mkdirSync(toolDir, { recursive: true });
  fs.writeFileSync(path.join(toolDir, 'extra-tools.json'), JSON.stringify({ enabled_tools: ['codegraph_callers'] }));

  const env = {
    SYSTEMROOT: process.env.SYSTEMROOT,
    TEMP: process.env.TEMP,
    TMP: process.env.TMP,
    CODEGRAPH_MCP_FAKE_SCRIPT: FAKE,
  };
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [SERVER, '--repo-root', REPO_ROOT, '--build-dir', buildDir],
    env,
    cwd: REPO_ROOT,
  });
  const client = new Client({ name: 'extra-tools-test', version: '0.0.1' });
  await client.connect(transport);
  try {
    await waitReady(client);
    const list = await client.listTools();
    assert.deepEqual(
      list.tools.map((tool) => tool.name).sort(),
      ['codegraph_callers', 'codegraph_explore', 'codegraph_status']
    );
    const result = await client.callTool({ name: 'codegraph_callers', arguments: { symbol: 'foo' } });
    assert.equal(result.isError, undefined);
  } finally {
    await client.close();
    await waitForPidExit(transport.pid);
    fs.rmSync(buildDir, { recursive: true, force: true });
  }
});

test('a small upstream project can hide status discovery while retaining a working status handler', async () => {
  const { client, transport, buildDir } = await launchServer({ CODEGRAPH_MCP_FAKE_SCRIPT: FAKE, CODEGRAPH_FAKE_HIDE_STATUS: '1' });
  try {
    await waitReady(client);
    const response = await client.callTool({ name: 'codegraph_explore', arguments: { query: 'small project' } });
    assert.notEqual(response.isError, true);
    const status = textOf(await client.callTool({ name: 'codegraph_status', arguments: {} }));
    assert.equal(status.wrapper_state, 'ready');
    assert.notEqual(status.backend.isError, true);
  } finally {
    await client.close();
    await waitForPidExit(transport.pid);
    fs.rmSync(buildDir, { recursive: true, force: true });
  }
});

test('not-ready wrapper answers INDEX_NOT_READY and status reports the state', async () => {
  const { client, transport, buildDir } = await launchServer({
    CODEGRAPH_MCP_FAKE_SCRIPT: FAKE,
    CODEGRAPH_FAKE_NO_STATUS: '1', // backend never lists status -> allowlist check degrades
  });
  try {
    const result = await client.callTool({ name: 'codegraph_explore', arguments: { query: 'x' } });
    const payload = textOf(result);
    assert.ok(
      payload.code === 'INDEX_NOT_READY' || payload.code === 'BACKEND_FAILED',
      `expected a not-ready/failed code, got ${payload.code}`
    );
    // The allowlist check degrades the wrapper once tools/list answers; poll
    // until that verdict lands rather than racing the background startup.
    let status = null;
    const deadline = Date.now() + 10_000;
    while (Date.now() < deadline) {
      status = textOf(await client.callTool({ name: 'codegraph_status', arguments: {} }));
      if (status.wrapper_state === 'degraded') {
        break;
      }
      await new Promise((resolve) => {
        setTimeout(resolve, 200);
      });
    }
    assert.equal(status.wrapper_state, 'degraded');
    assert.match(status.state_detail, /codegraph_status/);
  } finally {
    await client.close();
    await waitForPidExit(transport.pid);
    fs.rmSync(buildDir, { recursive: true, force: true });
  }
});
