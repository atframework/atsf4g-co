import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { loadSdk } from '../../../common/src/sdk.mjs';
const { Client } = await loadSdk('tools/tgrep', '@modelcontextprotocol/client');
const { StdioClientTransport } = await loadSdk('tools/tgrep', '@modelcontextprotocol/client/stdio');

const HERE = path.dirname(fileURLToPath(import.meta.url));
const SERVER = path.join(HERE, '..', 'src', 'server.mjs');
const FAKE = path.join(HERE, 'fake-tgrep-serve.mjs');
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..', '..', '..');

function tmpBuildDir() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-tgrep-server-'));
}

async function launchServer(envExtra = {}, entry = SERVER) {
  const buildDir = tmpBuildDir();
  const transport = new StdioClientTransport({
    command: process.execPath,
    args: [entry, '--repo-root', buildDir],
    env: {
      ...envExtra,
      SYSTEMROOT: process.env.SYSTEMROOT,
      TEMP: process.env.TEMP,
      TMP: process.env.TMP,
    },
    cwd: buildDir,
  });
  const client = new Client({ name: 'tgrep-server-test', version: '0.0.1' });
  await client.connect(transport);
  return { client, transport, buildDir };
}

test('legacy tgrep server entry still serves MCP and forwards module exports', async () => {
  const old = await import('../../../tgrep/src/backend.mjs');
  const current = await import('../src/backend.mjs');
  assert.equal(old.TgrepBackend, current.TgrepBackend);
  const { client, transport, buildDir } = await launchServer({ TGREP_MCP_FAKE_SCRIPT: FAKE }, path.resolve(HERE, '../../../tgrep/src/server.mjs'));
  try {
    assert.ok((await client.listTools()).tools.some(tool => tool.name === 'tgrep_search'));
  } finally {
    await client.close();
    assert.ok(await waitForServerExit(transport.pid, buildDir));
    fs.rmSync(buildDir, { recursive: true, force: true });
  }
});

function textOf(result) {
  assert.equal(result.content[0].type, 'text');
  return JSON.parse(result.content[0].text);
}

// The stdio wrapper and its detached owner have separate process lifetimes.
// Wait for both before removing their working directory on Windows.
async function waitForServerExit(pid, root) {
  const { WorkspacePaths } = await import('../../../common/src/paths.mjs');
  const exited = await waitForPidExit(pid);
  const record = path.join(new WorkspacePaths(root).toolStateDir('tgrep'), 'shared-service.json');
  if (fs.existsSync(record)) {
    const owner = JSON.parse(fs.readFileSync(record, 'utf8'));
    assert.ok(await waitForPidExit(owner.pid), 'shared owner must exit before fixture removal');
  }
  return exited;
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

test('handshake exposes the fixed three-tool list', async () => {
  const { client, transport, buildDir } = await launchServer({ TGREP_MCP_FAKE_SCRIPT: FAKE });
  try {
    const list = await client.listTools();
    assert.deepEqual(
      list.tools.map((tool) => tool.name).sort(),
      ['tgrep_files', 'tgrep_search', 'tgrep_status']
    );
    for (const tool of list.tools) {
      assert.equal(tool.inputSchema.type, 'object');
    }
  } finally {
    await client.close();
    const exited = await waitForServerExit(transport.pid, buildDir);
    assert.ok(exited, 'server process must exit after client close');
    fs.rmSync(buildDir, { recursive: true, force: true });
  }
});

async function waitReady(client, timeoutMs = 15_000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const status = textOf(await client.callTool({ name: 'tgrep_status', arguments: {} }));
    if (status.wrapper_state === 'ready') {
      return status;
    }
    await new Promise((resolve) => {
      setTimeout(resolve, 200);
    });
  }
  throw new Error('wrapper did not reach ready in time');
}

test('tgrep_search proxies matches; bad scope is rejected', async () => {
  const { client, transport, buildDir } = await launchServer({ TGREP_MCP_FAKE_SCRIPT: FAKE });
  try {
    await waitReady(client);
    const ok = await client.callTool({
      name: 'tgrep_search',
      arguments: { pattern: 'hello', scope: 'src', context: 1 },
    });
    const payload = textOf(ok);
    assert.equal(ok.isError, undefined);
    assert.equal(payload.num_matches, 2);
    assert.match(payload.matches[0].text, /hello/);

    const bad = await client.callTool({ name: 'tgrep_search', arguments: { pattern: 'x', scope: '../escape' } });
    assert.equal(bad.isError, true);
    assert.equal(textOf(bad).code, 'INVALID_PARAMS');
  } finally {
    await client.close();
    await waitForServerExit(transport.pid, buildDir);
    fs.rmSync(buildDir, { recursive: true, force: true });
  }
});

test('queries before the index is ready fail with INDEX_NOT_READY', async () => {
  const { client, transport, buildDir } = await launchServer({
    TGREP_MCP_FAKE_SCRIPT: FAKE,
    TGREP_FAKE_NEVER_READY: '1',
  });
  try {
    const result = await client.callTool({ name: 'tgrep_search', arguments: { pattern: 'anything' } });
    assert.equal(result.isError, true);
    assert.equal(textOf(result).code, 'INDEX_NOT_READY');

    const status = await client.callTool({ name: 'tgrep_status', arguments: {} });
    // Either pre-ready phase is valid depending on spawn timing.
    assert.ok(['starting', 'initializing'].includes(textOf(status).wrapper_state));
  } finally {
    await client.close();
    await waitForServerExit(transport.pid, buildDir);
    fs.rmSync(buildDir, { recursive: true, force: true });
  }
});

test('a second Agent shares the backend and survives the first Agent closing', async () => {
  const buildDir = tmpBuildDir();
  const env = {
    SYSTEMROOT: process.env.SYSTEMROOT,
    TEMP: process.env.TEMP,
    TMP: process.env.TMP,
    TGREP_MCP_FAKE_SCRIPT: FAKE,
  };
  const first = new StdioClientTransport({
    command: process.execPath,
    args: [SERVER, '--repo-root', buildDir],
    env,
    cwd: buildDir,
  });
  const firstClient = new Client({ name: 'first', version: '0.0.1' });
  await firstClient.connect(first);
  try {
    // Wait for the first instance to grab the lock (startup runs post-connect).
    await new Promise((resolve) => {
      setTimeout(resolve, 500);
    });
    const second = new StdioClientTransport({
      command: process.execPath,
      args: [SERVER, '--repo-root', buildDir],
      env,
      cwd: buildDir,
    });
    const secondClient = new Client({ name: 'second', version: '0.0.1' });
    await secondClient.connect(second);
    try {
      await waitReady(firstClient);
      const status = await secondClient.callTool({ name: 'tgrep_status', arguments: {} });
      assert.equal(textOf(status).wrapper_state, 'ready');
      assert.equal(textOf(status).shared_service.clients, 2);
      const firstStatus = textOf(await firstClient.callTool({ name: 'tgrep_status', arguments: {} }));
      assert.equal(textOf(status).shared_service.pid, firstStatus.shared_service.pid);
      await firstClient.close();
      assert.ok(await waitForPidExit(first.pid));
      const search = await secondClient.callTool({ name: 'tgrep_search', arguments: { pattern: 'x' } });
      assert.notEqual(search.isError, true);
      assert.equal(textOf(search).num_matches, 2);
    } finally {
      await secondClient.close();
      await waitForPidExit(second.pid);
    }
  } finally {
    await firstClient.close();
    await waitForPidExit(first.pid);
    fs.rmSync(buildDir, { recursive: true, force: true });
  }
});
