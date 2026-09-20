import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { agentStates, configureAgent, removeAgentServers, planAgentConfigChanges, applyAgentConfigChanges, runAgentConfigBatch } from '../src/writers.mjs';
import { createFileStore } from '../src/fileStore.mjs';
import { parseJsonDocument, removeServerEntry } from '../src/formats/jsonDocument.mjs';
import { TOML_BEGIN, TOML_END, removeCodexTomlServers } from '../src/formats/codexToml.mjs';

function workspace(t) {
  const repoRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-review-'));
  t.after(() => fs.rmSync(repoRoot, { recursive: true, force: true }));
  const options = { repoRoot, stateDir: path.join(repoRoot, 'state'), tmpDir: path.join(repoRoot, 'tmp') };
  const write = (relative, text) => {
    const file = path.join(repoRoot, relative);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, text);
    return file;
  };
  return { ...options, write, store: createFileStore(options) };
}

test('removing absent configs is a no-op for every product and creates no metadata', (t) => {
  const w = workspace(t);
  for (const agentId of ['codex', 'roo', 'kilo', 'codebuddy']) {
    assert.equal(removeAgentServers({ ...w, agentId }).action, 'unchanged');
  }
  assert.deepEqual(fs.readdirSync(w.repoRoot), []);
});

test('state scan recognizes generated Codex blocks and tolerates absent legacy files', (t) => {
  const w = workspace(t);
  for (const agentId of ['codex', 'roo', 'kilo']) configureAgent({ ...w, agentId, backend: 'tgrep' });
  const states = agentStates(w.repoRoot);
  for (const agentId of ['codex', 'roo', 'kilo']) {
    assert.equal(states[agentId].error, undefined);
    assert.deepEqual(states[agentId].configured, ['atsf4g-tgrep']);
  }
});

test('CodeBuddy legacy uninstall preserves foreign servers without creating .mcp.json', (t) => {
  const w = workspace(t);
  const file = w.write('mcp.json', '{"mcpServers":{"user":{"command":"uvx"}}}');
  configureAgent({ ...w, agentId: 'codebuddy', backend: 'tgrep' });
  removeAgentServers({ ...w, agentId: 'codebuddy' });
  assert.deepEqual(JSON.parse(fs.readFileSync(file, 'utf8')), { mcpServers: { user: { command: 'uvx' } } });
  assert.equal(fs.existsSync(path.join(w.repoRoot, '.mcp.json')), false);
});

test('JSONC duplicate keys separated from colons by comments are rejected', () => {
  assert.throws(() => parseJsonDocument('{"mcpServers" /* first */ :{},"mcpServers":{}}', 'f'),
    (error) => error.kind === 'duplicate-key');
});

test('JSONC removal preserves comments belonging to adjacent user entries', () => {
  for (const entries of [
    '"user":{}, // user note\n "atsf4g-tgrep":{}',
    '"atsf4g-tgrep":{}, // user note\n "user":{}',
  ]) {
    const after = removeServerEntry(`{"mcpServers":{${entries}}}`, 'mcpServers', 'atsf4g-tgrep');
    assert.match(after, /\/\/ user note/);
    assert.deepEqual(parseJsonDocument(after, 'f').root, { mcpServers: { user: {} } });
  }
});

test('TOML foreign multiline strings preserve blank lines and marker-looking content', () => {
  const text = `description = """\nfirst\n\n\n${TOML_BEGIN}\n[mcp_servers.atsf4g-tgrep]\n${TOML_END}\nlast\n"""\n`;
  assert.deepEqual(removeCodexTomlServers(text), { text, changed: false });
  const comment = '# ' + '"quoted"'.repeat(1000) + '\n';
  assert.deepEqual(removeCodexTomlServers(comment), { text: comment, changed: false });
});

test('TOML foreign quoted tables and brackets in comments end legacy cleanup', () => {
  const foreign = '[profiles."a]b"] # comment ]\nvalue = 42\n';
  const text = '[mcp_servers.atsf4g-tgrep]\ncommand = "node"\n' + foreign;
  assert.equal(removeCodexTomlServers(text).text, foreign);
});

test('TOML refuses foreign tables enclosed by managed markers', () => {
  assert.throws(() => removeCodexTomlServers(`${TOML_BEGIN}\n[profiles.user]\nvalue=42\n${TOML_END}\n`),
    (error) => error.kind === 'toml-marker');
});

test('reconfiguring a JSON server preserves its optional settings and comments', (t) => {
  const w = workspace(t);
  configureAgent({ ...w, agentId: 'cursor', backend: 'tgrep' });
  const file = path.join(w.repoRoot, '.cursor/mcp.json');
  const root = JSON.parse(fs.readFileSync(file, 'utf8'));
  root.mcpServers['atsf4g-tgrep'].env = { KEEP: 'sentinel' };
  root.mcpServers['atsf4g-tgrep'].timeout = 123;
  const before = JSON.stringify(root, null, 2).replace('"timeout":', '// keep this timeout\n      "timeout":') + '\n';
  fs.writeFileSync(file, before);
  configureAgent({ ...w, agentId: 'cursor', backend: 'tgrep' });
  assert.equal(fs.readFileSync(file, 'utf8'), before);
});

test('rollback does not recreate a file concurrently deleted after our update', (t) => {
  const w = workspace(t);
  const file = w.write('config.json', 'before');
  const step = { file, relative: 'config.json', before: 'before', after: 'after' };
  w.store.write(file, step.after, { relative: step.relative, expectedBefore: step.before });
  fs.rmSync(file);
  assert.equal(w.store.tryRestore(step), false);
  assert.equal(fs.existsSync(file), false);
});

test('rollback removes ownership of rolled-back creations and restores owned deletions', (t) => {
  const w = workspace(t);
  const step = { file: path.join(w.repoRoot, 'config.json'), relative: 'config.json', before: null, after: '{}' };
  w.store.write(step.file, step.after, { relative: step.relative, expectedBefore: null });
  assert.equal(w.store.tryRestore(step), true);
  w.write('config.json', '{}');
  assert.equal(w.store.owns(step.relative), false, 'user replacement is not installer-owned');
  fs.rmSync(step.file);
  w.store.write(step.file, step.after, { relative: step.relative, expectedBefore: null });
  w.store.remove(step.file, { relative: step.relative, expectedBefore: '{}' });
  assert.equal(w.store.tryRestore({ ...step, before: '{}', after: null, deleteFile: true }), true);
  assert.equal(w.store.owns(step.relative), true);
});

test('unreadable ownership state blocks writing a new config', (t) => {
  const w = workspace(t);
  w.write('state', 'block mkdir');
  const plan = planAgentConfigChanges({ ...w, operations: [{ type: 'configure', agentId: 'cursor', backend: 'tgrep' }] });
  assert.equal(plan.problems.length, 0);
  assert.throws(() => applyAgentConfigChanges({ ...w, plan }));
  assert.equal(fs.existsSync(path.join(w.repoRoot, '.cursor/mcp.json')), false);
});

test('a journal failure after rename rolls back every mutation including the failing file', (t) => {
  const w = workspace(t);
  const append = fs.appendFileSync;
  t.mock.method(fs, 'appendFileSync', (file, text, options) => {
    if (String(text).includes('"action":"created"') && String(text).includes('.cursor/mcp.json')) throw new Error('injected journal failure');
    return append(file, text, options);
  });
  const plan = planAgentConfigChanges({ ...w, operations: ['claude', 'cursor'].map((agentId) => ({ type: 'configure', agentId, backend: 'tgrep' })) });
  assert.throws(() => applyAgentConfigChanges({ ...w, plan }), (error) => {
    assert.equal(error.cause.message, 'injected journal failure');
    assert.deepEqual(error.rollback.map((r) => [r.relative, r.restored]), [['.cursor/mcp.json', true], ['.mcp.json', true]]);
    return true;
  });
  for (const file of ['.cursor/mcp.json', '.mcp.json']) {
    assert.equal(fs.existsSync(path.join(w.repoRoot, file)), false);
    assert.equal(w.store.owns(file), false);
  }
});

test('one failed restore does not prevent restoration of earlier files', (t) => {
  const w = workspace(t);
  const append = fs.appendFileSync;
  t.mock.method(fs, 'appendFileSync', (file, text, options) => {
    if (String(text).includes('"action":"created"') && String(text).includes('.cursor/mcp.json')) {
      fs.writeFileSync(path.join(w.repoRoot, '.cursor/mcp.json'), 'concurrent edit');
      throw new Error('injected journal failure');
    }
    if (String(text).includes('"action":"restore-skipped"')) throw new Error('injected recovery journal failure');
    return append(file, text, options);
  });
  const plan = planAgentConfigChanges({ ...w, operations: ['claude', 'cursor'].map((agentId) => ({ type: 'configure', agentId, backend: 'tgrep' })) });
  assert.throws(() => applyAgentConfigChanges({ ...w, plan }), (error) => {
    assert.deepEqual(error.rollback.map((r) => r.restored), [false, true]);
    assert.equal(error.cause.message, 'injected journal failure');
    return true;
  });
  assert.equal(fs.readFileSync(path.join(w.repoRoot, '.cursor/mcp.json'), 'utf8'), 'concurrent edit');
  assert.equal(fs.existsSync(path.join(w.repoRoot, '.mcp.json')), false);
});

test('same-named foreign JSON and TOML servers abort a batch without any writes', (t) => {
  const w = workspace(t);
  const json = '{"mcpServers":{"atsf4g-tgrep":{"command":"node","args":["elsewhere.mjs"]}}}';
  const toml = '[mcp_servers.atsf4g-tgrep]\ncommand="uvx"\nargs=["other"]\n';
  for (const [agentId, relative, content] of [['cursor', '.cursor/mcp.json', json], ['codex', '.codex/config.toml', toml]]) {
    const file = w.write(relative, content);
    const result = runAgentConfigBatch({ ...w, operations: [
      { type: 'configure', agentId: 'claude', backend: 'tgrep' }, { type: 'remove', agentId },
    ] });
    assert.equal(result.plan.problems[0]?.error.kind, 'server-conflict');
    assert.equal(result.applied, false);
    assert.equal(fs.readFileSync(file, 'utf8'), content);
    assert.equal(fs.existsSync(path.join(w.repoRoot, '.mcp.json')), false);
  }
});

test('Kilo layers coexist, root managed entries migrate using their declared format and retain BOM', (t) => {
  const w = workspace(t);
  const command = ['node', path.join(w.repoRoot, 'project/integration/mcp/tgrep/src/server.mjs')];
  const root = w.write('kilo.json', '\uFEFF' + JSON.stringify({ model: 'keep', mcp: { 'atsf4g-tgrep': { type: 'local', command, enabled: true }, user: { type: 'local', command: ['x'] } } }));
  w.write('.kilo/kilo.json', '{}');
  configureAgent({ ...w, agentId: 'kilocode', backend: 'tgrep' });
  const text = fs.readFileSync(root, 'utf8');
  assert.equal(text[0], '\uFEFF');
  assert.deepEqual(parseJsonDocument(text, root).root, { model: 'keep', mcp: { user: { type: 'local', command: ['x'] } } });
  assert.deepEqual(agentStates(w.repoRoot).kilo.configured, ['atsf4g-tgrep']);
});

test('legacy migration refuses to discard custom fields, but explicit uninstall can remove them', (t) => {
  const w = workspace(t);
  const original = JSON.stringify({ mcpServers: { 'atsf4g-tgrep': { command: 'node', args: [path.join(w.repoRoot, 'project/integration/mcp/tgrep/src/server.mjs')], env: { KEEP: 'value' } } } });
  const file = w.write('.roo/mcp_settings.json', original);
  assert.throws(() => configureAgent({ ...w, agentId: 'roo', backend: 'tgrep' }), (error) => error.kind === 'legacy-options-conflict');
  assert.equal(fs.readFileSync(file, 'utf8'), original);
  assert.equal(fs.existsSync(path.join(w.repoRoot, '.roo/mcp.json')), false);
  removeAgentServers({ ...w, agentId: 'roo' });
  assert.deepEqual(JSON.parse(fs.readFileSync(file, 'utf8')), { mcpServers: {} });
});

test('Codex repeated setup preserves optional fields, escaped paths, CRLF and comments', (t) => {
  const w = workspace(t);
  configureAgent({ ...w, agentId: 'codex', backend: 'tgrep' });
  const file = path.join(w.repoRoot, '.codex/config.toml');
  const before = fs.readFileSync(file, 'utf8').replace(TOML_END, `startup_timeout_sec = 123 # keep\n[mcp_servers.atsf4g-tgrep.env]\nKEEP = "sentinel"\n${TOML_END}`).replace(/\n/g, '\r\n');
  fs.writeFileSync(file, before);
  assert.equal(configureAgent({ ...w, agentId: 'codex', backend: 'tgrep' }).action, 'unchanged');
  assert.equal(fs.readFileSync(file, 'utf8'), before);
});

test('inline or dotted TOML managed definitions are refused instead of duplicated', () => {
  for (const text of ['mcp_servers.atsf4g-tgrep.command="node"\n', '[mcp_servers]\natsf4g-tgrep={command="node"}\n']) {
    assert.throws(() => removeCodexTomlServers(text), (error) => error.kind === 'toml-shape');
  }
});

test('planning refuses a directory junction outside the repository before any target write', (t) => {
  const w = workspace(t);
  const outside = workspace(t);
  outside.write('mcp.json', '{"mcpServers":{}}');
  const link = path.join(w.repoRoot, '.cursor');
  fs.symlinkSync(outside.repoRoot, link, process.platform === 'win32' ? 'junction' : 'dir');
  t.after(() => { if (fs.existsSync(link)) fs.unlinkSync(link); });
  const plan = planAgentConfigChanges({ ...w, operations: [
    { type: 'configure', agentId: 'claude', backend: 'tgrep' },
    { type: 'configure', agentId: 'cursor', backend: 'tgrep' },
  ] });
  assert.equal(plan.problems[0]?.error.kind, 'outside-repo');
  assert.equal(fs.existsSync(path.join(w.repoRoot, '.mcp.json')), false);
});
