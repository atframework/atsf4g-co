import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { configureAgent, removeAgentServers } from '../src/writers.mjs';

test('legacy wrapper locations migrate to tools without losing settings, and uninstall remains scoped', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-layout-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const launch = { integrationRoot: path.join(root, 'kit') };
  const file = path.join(root, '.mcp.json');
  for (const backend of ['tgrep', 'codegraph']) {
    const oldEntry = { command: 'node', args: [path.join(launch.integrationRoot, backend, 'src/server.mjs'), '--repo-root', root], disabled: true, env: { KEEP: 'yes' } };
    fs.writeFileSync(file, JSON.stringify({ mcpServers: { [`workspace-${backend}`]: oldEntry, other: { command: 'foreign' } } }));
    configureAgent({ repoRoot: root, agentId: 'claude', backend, launch });
    const updated = JSON.parse(fs.readFileSync(file, 'utf8'));
    assert.equal(updated.mcpServers[`workspace-${backend}`].args[0], path.join(launch.integrationRoot, 'tools', backend, 'src/server.mjs'));
    assert.equal(updated.mcpServers[`workspace-${backend}`].disabled, true);
    assert.deepEqual(updated.mcpServers[`workspace-${backend}`].env, { KEEP: 'yes' });
    removeAgentServers({ repoRoot: root, agentId: 'claude', launch });
    assert.deepEqual(JSON.parse(fs.readFileSync(file, 'utf8')).mcpServers, { other: { command: 'foreign' } });
  }
});
