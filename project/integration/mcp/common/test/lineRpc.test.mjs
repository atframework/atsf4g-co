import assert from 'node:assert/strict';
import test from 'node:test';

import { BackendError, IndexNotReady } from '../src/errors.mjs';
import { LineJsonRpcClient } from '../src/lineRpc.mjs';
import { minimalEnvironment, supervise } from '../src/supervisor.mjs';

const FAKE = `
const lines = require('readline').createInterface({ input: process.stdin });
const reply = (msg) => process.stdout.write(JSON.stringify(msg) + '\\n');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let failNext = false;
lines.on('line', async (line) => {
  if (!line.trim()) return;
  const req = JSON.parse(line);
  if (req.method === 'fail-next') { failNext = true; reply({ jsonrpc: '2.0', id: req.id, result: { ok: true } }); return; }
  if (req.method === 'echo') {
    if (failNext) { failNext = false; reply({ jsonrpc: '2.0', id: req.id, error: { code: req.params.code, message: 'injected' } }); return; }
    reply({ jsonrpc: '2.0', id: req.id, result: { echoed: req.params.value } }); return;
  }
  if (req.method === 'slow') { await sleep(req.params.ms); reply({ jsonrpc: '2.0', id: req.id, result: { done: true } }); return; }
  reply({ jsonrpc: '2.0', id: req.id, error: { code: -32601, message: 'unknown method' } });
});
process.stdin.on('end', () => process.exit(0));
`.trim();

async function withFakeClient(run) {
  const proc = supervise({
    argv: [process.execPath, '-e', FAKE],
    cwd: process.cwd(),
    env: minimalEnvironment(),
    label: 'fake-rpc',
  });
  const client = new LineJsonRpcClient(proc);
  try {
    await run(client);
  } finally {
    client.close();
    await proc.stop();
  }
}

test('request resolves matched responses and ignores stale lines', async () => {
  await withFakeClient(async (client) => {
    const result = await client.request('echo', { value: 42 });
    assert.deepEqual(result, { echoed: 42 });
    const again = await client.request('echo', { value: 'second' });
    assert.deepEqual(again, { echoed: 'second' });
  });
});

test('backend error codes map to wrapper error classes', async () => {
  await withFakeClient(async (client) => {
    await client.request('fail-next', {});
    await assert.rejects(() => client.request('echo', { value: 1, code: -32001 }), IndexNotReady);
    await client.request('fail-next', {});
    await assert.rejects(
      () => client.request('echo', { value: 1, code: -32002 }),
      (error) => error instanceof BackendError && error.code === 'INDEX_INVALID'
    );
    await assert.rejects(
      () => client.request('nope', {}),
      (error) => error instanceof BackendError && error.code === 'BACKEND_FAILED'
    );
  });
});

test('slow responses time out as BACKEND_TIMEOUT', async () => {
  await withFakeClient(async (client) => {
    await assert.rejects(
      () => client.request('slow', { ms: 2000 }, 200),
      (error) => error.code === 'BACKEND_TIMEOUT'
    );
    // The channel stays usable after a timeout: the late reply is dropped.
    const result = await client.request('echo', { value: 'after-timeout' });
    assert.deepEqual(result, { echoed: 'after-timeout' });
  });
});

test('backend death fails pending and future requests', async () => {
  const proc = supervise({
    argv: [process.execPath, '-e', "process.stdin.once('data', () => process.exit(1))"],
    cwd: process.cwd(),
    env: minimalEnvironment(),
  });
  const client = new LineJsonRpcClient(proc);
  await assert.rejects(
    () => client.request('echo', { value: 1 }, 5000),
    (error) => error.code === 'BACKEND_FAILED'
  );
  await assert.rejects(
    () => client.request('echo', { value: 2 }),
    (error) => error.code === 'BACKEND_FAILED'
  );
});
