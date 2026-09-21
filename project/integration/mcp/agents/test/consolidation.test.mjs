import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { parseJsonDocument } from '../src/formats/jsonDocument.mjs';

import {
  SERVER_IDS,
  agentStates,
  configureAgent,
  applyAgentConfigChanges,
  planAgentConfigChanges,
  removeAgentServers,
  runAgentConfigBatch,
} from '../src/writers.mjs';

/**
 * Exclusive-candidate consolidation: several existing
 * candidates merge into the first existing one; foreign content is carried
 * over, differing values abort before any write, redundant files are deleted
 * only after the merged destination was written, and every mutation is backed
 * up and rollback-able.
 */

function workspace(t) {
  const repoRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-consolidation-'));
  t.after(() => fs.rmSync(repoRoot, { recursive: true, force: true }));
  const options = { repoRoot, stateDir: path.join(repoRoot, 'state'), tmpDir: path.join(repoRoot, 'tmp') };
  const write = (relative, text) => {
    const file = path.join(repoRoot, relative);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, text);
    return file;
  };
  const read = (relative) => fs.readFileSync(path.join(repoRoot, relative), 'utf8');
  const exists = (relative) => fs.existsSync(path.join(repoRoot, relative));
  return { ...options, write, read, exists };
}

function ourEntry(w, backend = 'tgrep') {
  return { type: 'local', command: ['node', path.join(w.repoRoot, 'project/integration/mcp', backend, 'src/server.mjs')], enabled: true };
}

test('kilo consolidates the real-world dual-candidate layout ($schema files)', (t) => {
  const w = workspace(t);
  // Byte shapes taken from the repository that triggered the feature request.
  w.write('.kilo/kilo.json', '{\n  "$schema": "https://app.kilo.ai/config.json"\n}\n');
  w.write('.kilo/kilo.jsonc', '{\n  "$schema": "https://app.kilo.ai/config.json",\n  "snapshot": false\n}\n');
  const statesBefore = agentStates(w.repoRoot);
  assert.equal(statesBefore.kilo.error, undefined, 'multi-candidate is no longer a scan error');

  configureAgent({ ...w, agentId: 'kilo', backend: 'tgrep' });
  const merged = JSON.parse(w.read('.kilo/kilo.json'));
  assert.equal(merged.$schema, 'https://app.kilo.ai/config.json');
  assert.equal(merged.snapshot, false, 'foreign key carried over from the jsonc candidate');
  assert.equal(merged.mcp[SERVER_IDS.tgrep].type, 'local');
  assert.equal(w.exists('.kilo/kilo.jsonc'), false, 'redundant candidate deleted');
  assert.deepEqual(agentStates(w.repoRoot).kilo.configured, [SERVER_IDS.tgrep]);
});

test('dry-run previews consolidation without touching any candidate', (t) => {
  const w = workspace(t);
  w.write('.kilo/kilo.json', '{"a":1}\n');
  w.write('.kilo/kilo.jsonc', '{"b":{"nested":true}}\n');
  const result = runAgentConfigBatch({
    ...w, dryRun: true, operations: [{ type: 'configure', agentId: 'kilo', backend: 'tgrep' }],
  });
  assert.equal(result.applied, false);
  const actions = result.plan.steps.map((step) => [step.relative, step.action, step.consolidation ?? false]);
  assert.deepEqual(actions, [['.kilo/kilo.json', 'update', false], ['.kilo/kilo.jsonc', 'delete', true]]);
  assert.match(result.plan.notes.join('\n'), /合并/);
  assert.equal(w.read('.kilo/kilo.json'), '{"a":1}\n');
  assert.equal(w.read('.kilo/kilo.jsonc'), '{"b":{"nested":true}}\n');
});

test('commented destinations keep their comments through consolidation', (t) => {
  const w = workspace(t);
  w.write('.kilo/kilo.json', '{\n  // kilo note\n  "mcp": { "user": { "type": "local", "command": ["x"] } }\n}\n');
  w.write('.kilo/kilo.jsonc', '{ "mcp": { "other": { "type": "local", "command": ["y"] } } }\n');
  configureAgent({ ...w, agentId: 'kilo', backend: 'codegraph' });
  const text = w.read('.kilo/kilo.json');
  assert.match(text, /\/\/ kilo note/);
  const merged = JSON.parse(text.replace(/\/\/[^\n\r]*/g, ''));
  assert.ok(merged.mcp.user && merged.mcp.other, 'foreign entries from both candidates kept');
  assert.ok(merged.mcp[SERVER_IDS.codegraph], 'managed entry added');
});

test('differing foreign values between candidates abort with zero writes', (t) => {
  const w = workspace(t);
  w.write('.kilo/kilo.json', '{"snapshot": true}\n');
  w.write('.kilo/kilo.jsonc', '{"snapshot": false}\n');
  assert.throws(
    () => configureAgent({ ...w, agentId: 'kilo', backend: 'tgrep' }),
    (error) => error.kind === 'consolidation-conflict' && /snapshot/.test(error.message),
  );
  assert.equal(w.read('.kilo/kilo.json'), '{"snapshot": true}\n');
  assert.equal(w.read('.kilo/kilo.jsonc'), '{"snapshot": false}\n');
});

test('differing foreign server entries abort; managed entries are normalized instead', (t) => {
  const w = workspace(t);
  w.write('.kilo/kilo.json', '{"mcp":{"user":{"command":["a"]}}}\n');
  w.write('.kilo/kilo.jsonc', '{"mcp":{"user":{"command":["b"]}}}\n');
  assert.throws(
    () => configureAgent({ ...w, agentId: 'kilo', backend: 'tgrep' }),
    (error) => error.kind === 'consolidation-conflict' && /user/.test(error.message),
  );

  const w2 = workspace(t);
  // Managed ids present in both candidates with different shapes: the planner
  // normalizes them into the selected backend entry — not a conflict.
  w2.write('.kilo/kilo.json', JSON.stringify({ mcp: { [SERVER_IDS.codegraph]: ourEntry(w2, 'codegraph') } }));
  w2.write('.kilo/kilo.jsonc', JSON.stringify({ mcp: { [SERVER_IDS.tgrep]: ourEntry(w2, 'tgrep') } }));
  configureAgent({ ...w2, agentId: 'kilo', backend: 'tgrep' });
  const merged = JSON.parse(w2.read('.kilo/kilo.json'));
  assert.ok(merged.mcp[SERVER_IDS.tgrep], 'selected backend wins');
  assert.equal(merged.mcp[SERVER_IDS.codegraph], undefined, 'other managed backend removed');
  assert.equal(w2.exists('.kilo/kilo.jsonc'), false);
});

test('a foreign same-name managed entry aborts the consolidation batch', (t) => {
  const w = workspace(t);
  w.write('.kilo/kilo.json', '{}\n');
  w.write('.kilo/kilo.jsonc', JSON.stringify({ mcp: { [SERVER_IDS.tgrep]: { type: 'local', command: ['node', 'elsewhere.mjs'] } } }));
  const result = runAgentConfigBatch({
    ...w, operations: [
      { type: 'configure', agentId: 'claude', backend: 'tgrep' },
      { type: 'configure', agentId: 'kilo', backend: 'tgrep' },
    ],
  });
  assert.equal(result.applied, false);
  assert.equal(result.plan.problems[0].error.kind, 'server-conflict');
  assert.equal(w.exists('.mcp.json'), false, 'the whole batch wrote nothing');
  assert.equal(w.read('.kilo/kilo.json'), '{}\n');
});

test('a damaged candidate keeps the old abort semantics (zero writes)', (t) => {
  const w = workspace(t);
  w.write('.kilo/kilo.json', '{"ok":true}\n');
  w.write('.kilo/kilo.jsonc', '{ broken');
  const result = runAgentConfigBatch({ ...w, operations: [{ type: 'configure', agentId: 'kilo', backend: 'tgrep' }] });
  assert.equal(result.applied, false);
  assert.equal(result.plan.problems[0].error.kind, 'invalid-json');
  assert.equal(w.read('.kilo/kilo.json'), '{"ok":true}\n');
  assert.equal(w.read('.kilo/kilo.jsonc'), '{ broken');
});

test('removal-only batches clean managed entries from every candidate', (t) => {
  const w = workspace(t);
  w.write('.kilo/kilo.json', JSON.stringify({ mcp: { [SERVER_IDS.tgrep]: ourEntry(w), user: { command: ['x'] } } }));
  w.write('.kilo/kilo.jsonc', JSON.stringify({ mcp: { [SERVER_IDS.codegraph]: ourEntry(w, 'codegraph') }, other: 1 }));
  const states = agentStates(w.repoRoot);
  assert.deepEqual(states.kilo.configured.sort(), [SERVER_IDS.codegraph, SERVER_IDS.tgrep], 'scan unions all candidates');
  removeAgentServers({ ...w, agentId: 'kilo' });
  assert.deepEqual(JSON.parse(w.read('.kilo/kilo.json')), { mcp: { user: { command: ['x'] } } }, 'foreign entry kept');
  assert.deepEqual(JSON.parse(w.read('.kilo/kilo.jsonc')), { mcp: {}, other: 1 }, 'both files cleaned in place, nothing consolidated on removal');
});

test('a failure after the merged write rolls back destination and redundant candidate', (t) => {
  const w = workspace(t);
  const beforeJson = '{"a":1}\n';
  const beforeJsonc = '{"b":2}\n';
  w.write('.kilo/kilo.json', beforeJson);
  w.write('.kilo/kilo.jsonc', beforeJsonc);
  // Also configure claude so a SECOND target follows the consolidation steps;
  // its rename is mocked to fail, forcing a rollback of everything before it.
  const rename = fs.renameSync;
  t.mock.method(fs, 'renameSync', (source, destination) => {
    if (String(destination).endsWith(path.join('.mcp.json'))) throw new Error('injected later-target failure');
    return rename(source, destination);
  });
  assert.throws(() => runAgentConfigBatch({
    ...w, operations: [
      { type: 'configure', agentId: 'kilo', backend: 'tgrep' },
      { type: 'configure', agentId: 'claude', backend: 'tgrep' },
    ],
  }), /injected later-target failure/);
  assert.equal(w.read('.kilo/kilo.json'), beforeJson, 'destination restored to its original bytes');
  assert.equal(w.read('.kilo/kilo.jsonc'), beforeJsonc, 'deleted redundant candidate restored');
  assert.equal(w.exists('.mcp.json'), false);
});

test('OpenCode combines same-directory candidates while MiMo preserves separate layers', (t) => {
  const w = workspace(t);
  w.write('opencode.json', '{"theme":"dark"}\n');
  w.write('opencode.jsonc', '{"mcp":{"shared":{"command":["z"]}}}\n');
  configureAgent({ ...w, agentId: 'opencode', backend: 'tgrep' });
  const opencode = JSON.parse(w.read('opencode.json'));
  assert.equal(opencode.theme, 'dark');
  assert.ok(opencode.mcp.shared && opencode.mcp[SERVER_IDS.tgrep]);
  assert.equal(w.exists('opencode.jsonc'), false);

  const w2 = workspace(t);
  w2.write('mimocode.jsonc', '{"model":"m"}\n');
  w2.write(path.join('.mimocode', 'mimocode.json'), '{"mcp":{"team":{"command":["t"]}}}\n');
  configureAgent({ ...w2, agentId: 'mimocode', backend: 'tgrep' });
  assert.equal(w2.read('mimocode.jsonc'), '{"model":"m"}\n');
  const mimo = JSON.parse(w2.read(path.join('.mimocode', 'mimocode.json')));
  assert.ok(mimo.mcp.team && mimo.mcp[SERVER_IDS.tgrep]);
  assert.equal(w2.exists(path.join('.mimocode', 'mimocode.json')), true);
});

test('consolidated writes appear in backups and the journal', (t) => {
  const w = workspace(t);
  w.write('.kilo/kilo.json', '{"a":1}\n');
  w.write('.kilo/kilo.jsonc', '{"b":2}\n');
  configureAgent({ ...w, agentId: 'kilo', backend: 'tgrep' });
  const journal = fs.readFileSync(path.join(w.tmpDir, 'agent-config-journal.jsonl'), 'utf8');
  assert.match(journal, /"file":"\.kilo\/kilo\.json"/);
  assert.match(journal, /"file":"\.kilo\/kilo\.jsonc"/);
  assert.match(journal, /"action":"deleted","file":"\.kilo\/kilo\.jsonc"/);
  const backups = fs.readdirSync(path.join(w.tmpDir, 'agent-config-backups'));
  assert.equal(backups.length, 2, 'both the destination rewrite and the redundant deletion are backed up');
});

test('an already configured destination still writes merged foreign data before deleting the source', (t) => {
  const w = workspace(t);
  const destination = '// keep destination\n' + JSON.stringify({ mcp: { [SERVER_IDS.tgrep]: ourEntry(w) } }) + '\n';
  w.write('.kilo/kilo.json', destination);
  w.write('.kilo/kilo.jsonc', '{"snapshot":false}');
  configureAgent({ ...w, agentId: 'kilo', backend: 'tgrep' });
  const merged = parseJsonDocument(w.read('.kilo/kilo.json'), 'test').root;
  assert.equal(merged.snapshot, false);
  assert.equal(w.exists('.kilo/kilo.jsonc'), false);
});

test('consolidation preserves source comments, destination BOM and CRLF, and is idempotent', (t) => {
  const w = workspace(t);
  w.write('.kilo/kilo.json', '\uFEFF{\r\n  "snapshot": false\r\n}\r\n');
  w.write('.kilo/kilo.jsonc', '// why this server exists\n{"mcp":{"team":{/* keep its reason */"command":["tool"]}}}\n// final note');
  configureAgent({ ...w, agentId: 'kilo', backend: 'tgrep' });
  const merged = w.read('.kilo/kilo.json');
  assert.ok(merged.startsWith('\uFEFF'));
  assert.match(merged, /why this server exists/);
  assert.match(merged, /keep its reason/);
  assert.match(merged, /final note/);
  assert.equal(merged.replace(/\r\n/g, '').includes('\n'), false);
  const before = fs.statSync(path.join(w.repoRoot, '.kilo/kilo.json')).mtimeMs;
  configureAgent({ ...w, agentId: 'kilo', backend: 'tgrep' });
  assert.equal(w.read('.kilo/kilo.json'), merged);
  assert.equal(fs.statSync(path.join(w.repoRoot, '.kilo/kilo.json')).mtimeMs, before);
});

test('compatible nested settings and managed options merge without losing fields', (t) => {
  const w = workspace(t);
  w.write('.kilo/kilo.json', JSON.stringify({ provider: { one: { options: { keep: true } } }, mcp: {
    [SERVER_IDS.tgrep]: { ...ourEntry(w), environment: { FIRST: 'one' } },
  } }));
  w.write('.kilo/kilo.jsonc', JSON.stringify({ provider: { two: { options: { keep: true } } }, mcp: {
    [SERVER_IDS.tgrep]: { ...ourEntry(w), environment: { SECOND: 'two' } },
  } }));
  configureAgent({ ...w, agentId: 'kilo', backend: 'tgrep' });
  const merged = parseJsonDocument(w.read('.kilo/kilo.json'), 'test').root;
  assert.deepEqual(Object.keys(merged.provider).sort(), ['one', 'two']);
  assert.deepEqual(merged.mcp[SERVER_IDS.tgrep].environment, { FIRST: 'one', SECOND: 'two' });
});

test('conflicting managed options abort rather than silently choosing a candidate', (t) => {
  const w = workspace(t);
  const first = JSON.stringify({ mcp: { [SERVER_IDS.tgrep]: { ...ourEntry(w), enabled: false } } });
  const second = JSON.stringify({ mcp: { [SERVER_IDS.tgrep]: ourEntry(w) } });
  w.write('.kilo/kilo.json', first);
  w.write('.kilo/kilo.jsonc', second);
  assert.throws(() => configureAgent({ ...w, agentId: 'kilo', backend: 'tgrep' }), (error) => error.kind === 'consolidation-conflict');
  assert.equal(w.read('.kilo/kilo.json'), first);
  assert.equal(w.read('.kilo/kilo.jsonc'), second);
});

test('MiMo keeps root and directory settings in their original layers', (t) => {
  const w = workspace(t);
  const root = '{"plugin":["./root-plugin.mjs"],"model":"root"}';
  const directory = '{"plugin":["./local-plugin.mjs"],"model":"local"}';
  w.write('mimocode.jsonc', root);
  w.write('.mimocode/mimocode.json', directory);
  configureAgent({ ...w, agentId: 'mimocode', backend: 'tgrep' });
  assert.equal(w.read('mimocode.jsonc'), root);
  const local = parseJsonDocument(w.read('.mimocode/mimocode.json'), 'test').root;
  assert.deepEqual(local.plugin, ['./local-plugin.mjs']);
  assert.equal(local.model, 'local');
  assert.ok(local.mcp[SERVER_IDS.tgrep]);
});

test('legacy Kilo stdio options migrate into the current native format', (t) => {
  const w = workspace(t);
  const args = [path.join(w.repoRoot, 'project/integration/mcp/tgrep/src/server.mjs'), '--build-dir', 'build'];
  w.write('.kilocode/mcp.json', JSON.stringify({ mcpServers: {
    [SERVER_IDS.tgrep]: { command: 'node', args, env: { LIMIT: 'one' }, disabled: true },
    foreign: { command: 'company-tool' },
  } }));
  configureAgent({ ...w, agentId: 'kilo', backend: 'tgrep' });
  const migrated = parseJsonDocument(w.read('.kilo/kilo.json'), 'test').root.mcp[SERVER_IDS.tgrep];
  assert.deepEqual(migrated.command, ['node', ...args]);
  assert.deepEqual(migrated.environment, { LIMIT: 'one' });
  assert.equal(migrated.enabled, false);
  assert.deepEqual(JSON.parse(w.read('.kilocode/mcp.json')).mcpServers, { foreign: { command: 'company-tool' } });
});

test('same-format legacy migration retains custom options and comments', (t) => {
  const w = workspace(t);
  const old = { command: 'node', args: ourEntry(w).command.slice(1), env: { KEEP: 'value' }, alwaysAllow: ['tool'] };
  w.write('.roo/mcp_settings.json', JSON.stringify({ mcpServers: { [SERVER_IDS.tgrep]: old } }).replace('"env":', '/* deployment environment */"env":'));
  configureAgent({ ...w, agentId: 'roo', backend: 'tgrep' });
  const migrated = parseJsonDocument(w.read('.roo/mcp.json'), 'roo').root.mcpServers[SERVER_IDS.tgrep];
  assert.deepEqual(migrated.env, old.env);
  assert.deepEqual(migrated.alwaysAllow, old.alwaysAllow);
  assert.match(w.read('.roo/mcp.json'), /deployment environment/);
  assert.deepEqual(JSON.parse(w.read('.roo/mcp_settings.json')), { mcpServers: {} });
});

test('omitted legacy defaults preserve explicit native disabled state', (t) => {
  const w = workspace(t);
  w.write('.kilo/kilo.json', JSON.stringify({ mcp: { [SERVER_IDS.tgrep]: { ...ourEntry(w), enabled: false } } }));
  w.write('.kilocode/mcp.json', JSON.stringify({ mcpServers: { [SERVER_IDS.tgrep]: { command: 'node', args: ourEntry(w).command.slice(1), env: { KEEP: 'yes' } } } }));
  configureAgent({ ...w, agentId: 'kilo', backend: 'tgrep' });
  const entry = JSON.parse(w.read('.kilo/kilo.json')).mcp[SERVER_IDS.tgrep];
  assert.equal(entry.enabled, false);
  assert.deepEqual(entry.environment, { KEEP: 'yes' });
});

test('unsupported legacy fields and file substitutions abort every write', (t) => {
  for (const { options, backend } of [
    { options: { timeout: 123 }, backend: 'tgrep' },
    { options: { env: { TOKEN: '{file:./token.txt}' } }, backend: 'tgrep' },
    { options: { cwd: 'somewhere-else' }, backend: 'tgrep' },
    { options: { cwd: 'somewhere-else' }, backend: 'codegraph' },
  ]) {
    const w = workspace(t);
    const legacy = JSON.stringify({ mcpServers: { [SERVER_IDS.tgrep]: { command: 'node', args: ourEntry(w).command.slice(1), ...options } } });
    w.write('.kilocode/mcp.json', legacy);
    const result = runAgentConfigBatch({ ...w, operations: [
      { type: 'configure', agentId: 'claude', backend: 'tgrep' },
      { type: 'configure', agentId: 'kilo', backend },
    ] });
    assert.equal(result.applied, false);
    assert.equal(result.plan.problems[0].error.kind, 'legacy-options-conflict');
    assert.equal(w.read('.kilocode/mcp.json'), legacy);
    assert.equal(w.exists('.kilo/kilo.json'), false);
    assert.equal(w.exists('.mcp.json'), false);
    assert.equal(w.exists('tmp'), false);
  }
});

test('legacy option conflicts never overwrite native settings or clean the source', (t) => {
  const w = workspace(t);
  const native = JSON.stringify({ mcp: { [SERVER_IDS.tgrep]: { ...ourEntry(w), environment: { KEEP: 'native' } } } });
  const legacy = JSON.stringify({ mcpServers: { [SERVER_IDS.tgrep]: { command: 'node', args: ourEntry(w).command.slice(1), env: { KEEP: 'legacy' } } } });
  w.write('.kilo/kilo.json', native);
  w.write('.kilocode/mcp.json', legacy);
  assert.throws(() => configureAgent({ ...w, agentId: 'kilo', backend: 'tgrep' }), (error) => error.kind === 'consolidation-conflict');
  assert.equal(w.read('.kilo/kilo.json'), native);
  assert.equal(w.read('.kilocode/mcp.json'), legacy);
});

test('legacy migration restores both files after a later target fails', (t) => {
  const w = workspace(t);
  const legacy = JSON.stringify({ mcpServers: { [SERVER_IDS.tgrep]: { command: 'node', args: ourEntry(w).command.slice(1), env: { KEEP: 'value' } } } });
  w.write('.kilocode/mcp.json', legacy);
  const rename = fs.renameSync;
  t.mock.method(fs, 'renameSync', (source, destination) => {
    if (destination === path.join(w.repoRoot, '.mcp.json')) throw new Error('later write failed');
    return rename(source, destination);
  });
  assert.throws(() => runAgentConfigBatch({ ...w, operations: [
    { type: 'configure', agentId: 'kilo', backend: 'tgrep' },
    { type: 'configure', agentId: 'claude', backend: 'tgrep' },
  ] }), /later write failed/);
  assert.equal(w.read('.kilocode/mcp.json'), legacy);
  assert.equal(w.exists('.kilo/kilo.json'), false);
});

test('a concurrently edited consolidation source survives and the destination rolls back', (t) => {
  const w = workspace(t);
  const original = '{"snapshot":false}';
  const concurrent = '{"model":"changed externally"}';
  const destination = w.write('.kilo/kilo.json', original);
  const source = w.write('.kilo/kilo.jsonc', '{"model":"original"}');
  const rename = fs.renameSync;
  let injected = false;
  t.mock.method(fs, 'renameSync', (from, to) => {
    const result = rename(from, to);
    if (to === destination && !injected) {
      injected = true;
      fs.writeFileSync(source, concurrent);
    }
    return result;
  });
  assert.throws(() => configureAgent({ ...w, agentId: 'kilo', backend: 'tgrep' }), (error) => error.cause?.kind === 'concurrent-modification');
  assert.equal(w.read('.kilo/kilo.json'), original);
  assert.equal(w.read('.kilo/kilo.jsonc'), concurrent);
});

test('cleanup requires the merged destination even when its planned action is unchanged', (t) => {
  for (const legacy of [false, true]) {
    const w = workspace(t);
    w.write('.kilo/kilo.json', JSON.stringify({ snapshot: false, mcp: { [SERVER_IDS.tgrep]: ourEntry(w) } }));
    const source = legacy ? 'kilo.json' : '.kilo/kilo.jsonc';
    const sourceText = legacy ? JSON.stringify({ mcp: { [SERVER_IDS.tgrep]: ourEntry(w) } }) : '{"snapshot":false}';
    w.write(source, sourceText);
    const plan = planAgentConfigChanges({ ...w, operations: [{ type: 'configure', agentId: 'kilo', backend: 'tgrep' }] });
    assert.deepEqual(plan.problems, []);
    assert.equal(plan.steps[0].action, 'unchanged');
    const concurrent = '{"model":"changed concurrently"}';
    w.write('.kilo/kilo.json', concurrent);
    assert.throws(() => applyAgentConfigChanges({ ...w, plan }), (error) => error.cause?.kind === 'concurrent-modification');
    assert.equal(w.read('.kilo/kilo.json'), concurrent);
    assert.equal(w.read(source), sourceText);
    assert.equal(w.exists('tmp'), false, 'dependency validation runs before backing up or deleting the source');
  }
});
