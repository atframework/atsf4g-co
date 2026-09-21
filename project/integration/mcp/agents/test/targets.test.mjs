import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  SERVER_IDS,
  agentStates,
  buildAgentOperations,
  configureAgent,
  removeAgentServers,
} from '../src/writers.mjs';
import { GUIDED_IMPORTS } from '../src/guidance/ideExports.mjs';
import { autoConfigurableAgents, guidedAgents } from '../src/registry.mjs';

test('registry and guided-import descriptors stay consistent', () => {
  for (const agent of autoConfigurableAgents()) {
    assert.ok(agent.targetId, `${agent.id} declares a target`);
  }
  const guided = guidedAgents().map((agent) => agent.id).sort();
  assert.deepEqual(Object.keys(GUIDED_IMPORTS).sort(), guided, 'every guided product has snippet metadata and vice versa');
  for (const agent of guidedAgents()) {
    assert.equal(agentStates(fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-registry-')))[agent.id].configured.length, 0);
    const operations = buildAgentOperations({ states: {}, selectedIds: new Set([agent.id]), backend: 'tgrep' });
    assert.equal(operations.length, 0, 'guided products never produce repo operations');
  }
});

function tmpRepo() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-targets-'));
}

function write(repo, relative, content) {
  const file = path.join(repo, relative);
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, content, 'utf8');
  return file;
}

function read(repo, relative) {
  return fs.readFileSync(path.join(repo, relative), 'utf8');
}

function ourLegacyEntry(repo, backend = 'tgrep') {
  const entry = { 'project/integration/mcp/tgrep/src/server.mjs': null };
  void entry;
  return {
    type: 'stdio',
    command: 'node',
    args: [path.join(repo, 'project', 'integration', 'mcp', backend, 'src', 'server.mjs')],
  };
}

// -- candidate discovery ------------------------------------------------------------------------

test('exclusive candidates: same-directory json/jsonc files consolidate', () => {
  const repo = tmpRepo();
  try {
    write(repo, path.join('.kilo', 'kilo.jsonc'), '{\n  "mcp": { "user": { "type": "local", "command": ["x"] } } }\n');
    write(repo, path.join('.kilo', 'kilo.json'), '{}\n');
    configureAgent({ repoRoot: repo, agentId: 'kilo', backend: 'tgrep' });
    const merged = JSON.parse(read(repo, path.join('.kilo', 'kilo.json')));
    assert.ok(merged.mcp.user, 'foreign entry from the redundant candidate carried over');
    assert.equal(merged.mcp[SERVER_IDS.tgrep].type, 'local', 'managed entry written');
    assert.equal(fs.existsSync(path.join(repo, path.join('.kilo', 'kilo.jsonc'))), false, 'redundant candidate deleted after the merged write');
    const states = agentStates(repo);
    assert.equal(states.kilo.error, undefined);
    assert.deepEqual(states.kilo.configured, [SERVER_IDS.tgrep]);
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('a lone existing candidate is edited in place; none uses the target default', () => {
  const repo = tmpRepo();
  try {
    write(repo, 'mimocode.jsonc', '{\n  // mimo\n  "mcp": { "user": { "type": "local", "command": ["x"] } } }\n');
    configureAgent({ repoRoot: repo, agentId: 'mimocode', backend: 'tgrep' });
    const edited = read(repo, 'mimocode.jsonc');
    assert.match(edited, /\/\/ mimo/, 'JSONC comment preserved');
    const parsed = JSON.parse(edited.replace(/\/\/[^\n\r]*/g, ''));
    assert.ok(parsed.mcp.user, 'foreign entry preserved');
    assert.equal(parsed.mcp[SERVER_IDS.tgrep].type, 'local');
    assert.equal(fs.existsSync(path.join(repo, '.mimocode', 'mimocode.json')), false, 'no second file created');

    const fresh = tmpRepo();
    try {
      configureAgent({ repoRoot: fresh, agentId: 'kilo', backend: 'tgrep' });
      assert.ok(fs.existsSync(path.join(fresh, '.kilo', 'kilo.json')), 'default .kilo/kilo.json created');
    } finally {
      fs.rmSync(fresh, { recursive: true, force: true });
    }
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

// -- Kilo / Roo legacy migration ---------------------------------------------------------------

test('kilo migrates from .kilocode/mcp.json and cleans only verified entries', () => {
  const repo = tmpRepo();
  try {
    write(
      repo,
      path.join('.kilocode', 'mcp.json'),
      JSON.stringify({ mcpServers: { [SERVER_IDS.tgrep]: ourLegacyEntry(repo), 'user-tool': { command: 'uvx' } } }, null, 2),
    );
    configureAgent({ repoRoot: repo, agentId: 'kilo', backend: 'tgrep' });

    const kilo = JSON.parse(read(repo, path.join('.kilo', 'kilo.json')));
    assert.equal(kilo.mcp[SERVER_IDS.tgrep].type, 'local', 'new target written in the current shape');
    const legacy = JSON.parse(read(repo, path.join('.kilocode', 'mcp.json')));
    assert.equal(legacy.mcpServers[SERVER_IDS.tgrep], undefined, 'managed entry removed from legacy');
    assert.ok(legacy.mcpServers['user-tool'], 'foreign entry kept in legacy');

    // A same-named legacy entry that is NOT ours is reported and left alone.
    write(repo, path.join('.kilocode', 'mcp.json'), JSON.stringify({ mcpServers: { [SERVER_IDS.codegraph]: { command: 'other', args: ['/somewhere/else'] } } }, null, 2));
    configureAgent({ repoRoot: repo, agentId: 'kilo', backend: 'codegraph' });
    const notOurs = JSON.parse(read(repo, path.join('.kilocode', 'mcp.json')));
    assert.ok(notOurs.mcpServers[SERVER_IDS.codegraph], 'unverified same-named entry preserved');
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('roo migrates from .roo/mcp_settings.json to .roo/mcp.json', () => {
  const repo = tmpRepo();
  try {
    write(repo, path.join('.roo', 'mcp_settings.json'), JSON.stringify({ mcpServers: { [SERVER_IDS.codegraph]: ourLegacyEntry(repo, 'codegraph') } }, null, 2));
    configureAgent({ repoRoot: repo, agentId: 'roo', backend: 'tgrep' });
    const roo = JSON.parse(read(repo, path.join('.roo', 'mcp.json')));
    assert.ok(roo.mcpServers[SERVER_IDS.tgrep], 'new correct file written');
    const legacy = JSON.parse(read(repo, path.join('.roo', 'mcp_settings.json')));
    assert.equal(legacy.mcpServers[SERVER_IDS.codegraph], undefined, 'legacy cleaned');
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('uninstall reaches managed entries that only exist in legacy locations', () => {
  const repo = tmpRepo();
  try {
    write(repo, path.join('.kilocode', 'mcp.json'), JSON.stringify({ mcpServers: { [SERVER_IDS.tgrep]: ourLegacyEntry(repo) } }, null, 2));
    const states = agentStates(repo);
    assert.deepEqual(states.kilo.configured, [SERVER_IDS.tgrep], 'states detect the legacy entries');

    const result = removeAgentServers({ repoRoot: repo, agentId: 'kilo' });
    assert.notEqual(result.action, 'unchanged');
    const legacy = JSON.parse(read(repo, path.join('.kilocode', 'mcp.json')));
    assert.deepEqual(legacy, { mcpServers: {} }, 'legacy cleaned, file kept (no ownership record)');
    assert.equal(fs.existsSync(path.join(repo, '.kilo', 'kilo.json')), false, 'no new file created on uninstall');
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

// -- CodeBuddy legacy root mcp.json -------------------------------------------------------------

test('codebuddy alone edits the legacy root mcp.json in place', () => {
  const repo = tmpRepo();
  try {
    write(repo, 'mcp.json', JSON.stringify({ mcpServers: { 'user-tool': { command: 'uvx' } } }, null, 2));
    configureAgent({ repoRoot: repo, agentId: 'codebuddy', backend: 'tgrep' });
    const legacy = JSON.parse(read(repo, 'mcp.json'));
    assert.ok(legacy.mcpServers[SERVER_IDS.tgrep], 'managed entry added to the legacy file');
    assert.ok(legacy.mcpServers['user-tool'], 'foreign entry preserved');
    assert.equal(fs.existsSync(path.join(repo, '.mcp.json')), false, 'no shadowing .mcp.json created');
    assert.deepEqual(agentStates(repo).codebuddy.configured, [SERVER_IDS.tgrep]);
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('creating .mcp.json over a foreign-populated legacy mcp.json is refused', () => {
  const repo = tmpRepo();
  try {
    const content = JSON.stringify({ mcpServers: { 'user-tool': { command: 'uvx' } } }, null, 2);
    write(repo, 'mcp.json', content);
    assert.throws(
      () => configureAgent({ repoRoot: repo, agentId: 'claude', backend: 'tgrep' }),
      (error) => error.kind === 'legacy-shadow-conflict',
    );
    assert.equal(fs.existsSync(path.join(repo, '.mcp.json')), false, 'nothing written');
    assert.equal(read(repo, 'mcp.json'), content, 'legacy untouched');
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('a legacy mcp.json without foreign servers is cleaned after .mcp.json is created', () => {
  const repo = tmpRepo();
  try {
    write(repo, 'mcp.json', JSON.stringify({ mcpServers: { [SERVER_IDS.tgrep]: ourLegacyEntry(repo) } }, null, 2));
    configureAgent({ repoRoot: repo, agentId: 'claude', backend: 'codegraph' });
    const shared = JSON.parse(read(repo, '.mcp.json'));
    assert.ok(shared.mcpServers[SERVER_IDS.codegraph], 'shared file created with the new backend');
    const legacy = JSON.parse(read(repo, 'mcp.json'));
    assert.deepEqual(legacy, { mcpServers: {} }, 'legacy managed entry cleaned, file kept');
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('when both files exist the official first-existing candidate wins and the legacy is untouched', () => {
  const repo = tmpRepo();
  try {
    const legacyContent = JSON.stringify({ mcpServers: { [SERVER_IDS.tgrep]: ourLegacyEntry(repo), 'user-tool': { command: 'uvx' } } }, null, 2);
    write(repo, 'mcp.json', legacyContent);
    write(repo, '.mcp.json', JSON.stringify({ mcpServers: { 'other': { command: 'x' } } }, null, 2));
    configureAgent({ repoRoot: repo, agentId: 'codebuddy', backend: 'tgrep' });
    const shared = JSON.parse(read(repo, '.mcp.json'));
    assert.ok(shared.mcpServers[SERVER_IDS.tgrep]);
    assert.ok(shared.mcpServers.other, 'foreign kept in the effective file');
    assert.equal(read(repo, 'mcp.json'), legacyContent, 'legacy file left byte-identical');
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

// -- shared .mcp.json group -----------------------------------------------------------------------

test('shared group: selecting any member keeps the group for every consumer', () => {
  const repo = tmpRepo();
  try {
    configureAgent({ repoRoot: repo, agentId: 'claude', backend: 'tgrep' });
    const states = agentStates(repo);
    for (const id of ['claude', 'pi', 'codebuddy']) {
      assert.deepEqual(states[id].configured, [SERVER_IDS.tgrep], `${id} sees the shared file`);
    }
    // Selecting claude keeps the group: no removal is planned for pi/codebuddy.
    const ops = buildAgentOperations({ states, selectedIds: new Set(['claude']), backend: 'codegraph' });
    assert.deepEqual(
      ops.filter((op) => op.type === 'remove').map((op) => op.agentId),
      [],
      'no group member is removed while another member is selected',
    );
    // Deselecting the whole group removes it once for all three consumers.
    const opsOff = buildAgentOperations({ states, selectedIds: new Set(['zcode']), backend: 'tgrep' });
    assert.deepEqual(
      opsOff.filter((op) => op.type === 'remove').map((op) => op.agentId).sort(),
      ['claude', 'codebuddy', 'pi'],
      'deselecting the group removes it for every consumer',
    );
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});
