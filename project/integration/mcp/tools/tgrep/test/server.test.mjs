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
    args: [entry, '--repo-root', REPO_ROOT, '--build-dir', buildDir],
    env: {
      ...envExtra,
      SYSTEMROOT: process.env.SYSTEMROOT,
      TEMP: process.env.TEMP,
      TMP: process.env.TMP,
    },
    cwd: REPO_ROOT,
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
    assert.ok(await waitForPidExit(transport.pid));
    fs.rmSync(buildDir, { recursive: true, force: true });
  }
});

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
    const exited = await waitForPidExit(transport.pid);
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
    await waitForPidExit(transport.pid);
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
    await waitForPidExit(transport.pid);
    fs.rmSync(buildDir, { recursive: true, force: true });
  }
});

test('a second instance reports INDEX_IN_USE', async () => {
  const buildDir = tmpBuildDir();
  const env = {
    SYSTEMROOT: process.env.SYSTEMROOT,
    TEMP: process.env.TEMP,
    TMP: process.env.TMP,
    TGREP_MCP_FAKE_SCRIPT: FAKE,
  };
  const first = new StdioClientTransport({
    command: process.execPath,
    args: [SERVER, '--repo-root', REPO_ROOT, '--build-dir', buildDir],
    env,
    cwd: REPO_ROOT,
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
      args: [SERVER, '--repo-root', REPO_ROOT, '--build-dir', buildDir],
      env,
      cwd: REPO_ROOT,
    });
    const secondClient = new Client({ name: 'second', version: '0.0.1' });
    await secondClient.connect(second);
    try {
      // tgrep_status stays queryable in the lock-conflict state and embeds
      // the error payload; real queries fail with INDEX_IN_USE.
      const status = await secondClient.callTool({ name: 'tgrep_status', arguments: {} });
      assert.equal(textOf(status).wrapper_state, 'failed');
      assert.equal(textOf(status).error.code, 'INDEX_IN_USE');
      const search = await secondClient.callTool({ name: 'tgrep_search', arguments: { pattern: 'x' } });
      assert.equal(search.isError, true);
      assert.equal(textOf(search).code, 'INDEX_IN_USE');
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
