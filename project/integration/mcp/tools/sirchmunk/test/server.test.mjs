import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';
import { WorkspacePaths } from '../../../common/src/paths.mjs';
import { loadSdk } from '../../../common/src/sdk.mjs';
import { writeConfig } from '../src/config.mjs';
const { Client } = await loadSdk('tools/codegraph', '@modelcontextprotocol/client');
const { StdioClientTransport } = await loadSdk('tools/codegraph', '@modelcontextprotocol/client/stdio');
const HERE = path.dirname(fileURLToPath(import.meta.url));
const text = result => JSON.parse(result.content[0].text);

test('stdio handshake, scoped search, model status and EOF cleanup work without waiting for embedding', async t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-sirchmunk-server-'));
  const paths = new WorkspacePaths(root, path.join(root, 'build'));
  paths.ensureDirs();
  writeConfig(paths, { baseURL: 'https://llm.example.test/v1', apiKey: 'test-key-not-used', model: 'fixture' });
  fs.writeFileSync(paths.preparedStatePath(), JSON.stringify({ sirchmunk: { python: 'fake', model_dir: path.join(paths.downloadsDir, 'models/sirchmunk') } }));
  const transport = new StdioClientTransport({ command: process.execPath, args: [path.join(HERE, '../src/server.mjs'), '--repo-root', root],
    env: { ...process.env, SIRCHMUNK_MCP_FAKE_SCRIPT: path.join(HERE, 'fake-backend.mjs') }, stderr: 'pipe' });
  let diagnostic = '';
  transport.stderr?.on('data', chunk => { diagnostic += chunk; });
  const client = new Client({ name: 'sirchmunk-test', version: '1' });
  t.after(async () => { await client.close(); fs.rmSync(root, { recursive: true, force: true }); });
  await client.connect(transport);
  assert.deepEqual((await client.listTools()).tools.map(tool => tool.name).sort(), ['sirchmunk_get_cluster', 'sirchmunk_list_clusters', 'sirchmunk_search', 'sirchmunk_status']);
  let status;
  for (let attempt = 0; attempt < 100; attempt++) {
    status = text(await client.callTool({ name: 'sirchmunk_status', arguments: {} }));
    if (status.state === 'ready') break;
    await new Promise(resolve => setTimeout(resolve, 30));
  }
  assert.equal(status.state, 'ready', diagnostic);
  assert.equal(status.enable_knowledge_evolution, false);
  const found = text(await client.callTool({ name: 'sirchmunk_search', arguments: { query: 'RuntimeSmokeValue', paths: ['Source'] } }));
  assert.deepEqual(found.files, ['Source/example.cpp']);
  const rejected = await client.callTool({ name: 'sirchmunk_search', arguments: { query: 'secret', paths: ['../outside'] } });
  assert.equal(rejected.isError, true);
  await client.close();
  for (let attempt = 0; attempt < 100; attempt++) {
    try { process.kill(status.pid, 0); } catch { break; }
    await new Promise(resolve => setTimeout(resolve, 30));
  }
  assert.throws(() => process.kill(status.pid, 0));
});
