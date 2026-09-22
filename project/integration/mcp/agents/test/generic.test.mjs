import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { INTEGRATION_ROOT } from '../../common/src/paths.mjs';
import { runAgentConfigBatch, agentStates } from '../src/writers.mjs';
import { parseJsonDocument } from '../src/formats/jsonDocument.mjs';
import { GUIDED_IMPORTS, ideExportSnippetText, ideExportStates } from '../src/guidance/ideExports.mjs';

function workspace(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-generic-'));
  fs.mkdirSync(path.join(root, '.git'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const entry = { type: 'stdio', command: 'node', args: [path.join(INTEGRATION_ROOT, 'tgrep/src/server.mjs'), '--repo-root', root] };
  const write = (relative, text) => { const file = path.join(root, relative); fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, text); return file; };
  const run = (agentId, rest = {}) => runAgentConfigBatch({ repoRoot: root, operations: [{ type: 'configure', agentId, backend: 'tgrep' }], ...rest });
  return { root, entry, write, run };
}

for (const [agentId, relative, mapKey, flag, enabledValue] of [
  ['roo', '.roo/mcp.json', 'mcpServers', 'disabled', false],
  ['cline', '.cline/mcp.json', 'mcpServers', 'disabled', false],
  ['zed', '.zed/settings.json', 'context_servers', 'enabled', true],
  ['omp', '.omp/mcp.json', 'mcpServers', 'enabled', true],
  ['opencode', 'opencode.json', 'mcp', 'enabled', true],
  ['kilo', '.kilo/kilo.jsonc', 'mcp', 'enabled', true],
  ['mimocode', '.mimocode/mimocode.json', 'mcp', 'enabled', true],
]) {
  test(`${agentId}: default activation fills missing fields and preserves explicit disable`, t => {
    const w = workspace(t);
    assert.equal(w.run(agentId).applied, true);
    const file = path.join(w.root, relative);
    const config = JSON.parse(fs.readFileSync(file));
    assert.equal(config[mapKey]['workspace-tgrep'][flag], enabledValue);

    delete config[mapKey]['workspace-tgrep'][flag];
    config[mapKey].external = { command: 'external-server' };
    w.write(relative, JSON.stringify(config, null, 2));
    assert.equal(w.run(agentId).applied, true);
    assert.equal(JSON.parse(fs.readFileSync(file))[mapKey]['workspace-tgrep'][flag], enabledValue);

    config[mapKey]['workspace-tgrep'][flag] = !enabledValue;
    const disabled = JSON.stringify(config, null, 2).replace(`"${flag}":`, `/* keep disabled */ "${flag}":`);
    w.write(relative, disabled);
    const mtime = fs.statSync(file).mtimeMs;
    assert.equal(w.run(agentId).applied, true);
    assert.equal(fs.readFileSync(file, 'utf8'), disabled);
    assert.equal(fs.statSync(file).mtimeMs, mtime);
    assert.deepEqual(parseJsonDocument(disabled, file).root[mapKey].external, { command: 'external-server' });
  });
}

test('Codex defaults enabled in the parent table and retains explicit disable and tool permissions', t => {
  const w = workspace(t);
  assert.equal(w.run('codex').applied, true);
  const file = path.join(w.root, '.codex/config.toml');
  const fresh = fs.readFileSync(file, 'utf8');
  assert.match(fresh, /^enabled = true$/m);
  w.write('.codex/config.toml', [
    '[mcp_servers.workspace-tgrep]', 'command = "node"', `args = ${JSON.stringify(w.entry.args)}`,
    'disabled_tools = ["tool_to_keep_disabled"]', '[mcp_servers.workspace-tgrep.env]', 'KEEP = "yes"', '',
  ].join('\n'));
  assert.equal(w.run('codex').applied, true);
  const upgraded = fs.readFileSync(file, 'utf8');
  assert.match(upgraded, /enabled = true\n\[mcp_servers.workspace-tgrep.env\]/);
  const disabled = upgraded.replace('enabled = true', 'enabled = false # keep disabled');
  w.write('.codex/config.toml', disabled);
  const mtime = fs.statSync(file).mtimeMs;
  assert.equal(w.run('codex').applied, true);
  assert.equal(fs.readFileSync(file, 'utf8'), disabled);
  assert.equal(fs.statSync(file).mtimeMs, mtime);
  assert.match(disabled, /disabled_tools = \["tool_to_keep_disabled"\]/);
});

test('shared JSON formats do not gain another client activation field', t => {
  const w = workspace(t);
  for (const [agentId, relative, mapKey] of [
    ['vscode', '.vscode/mcp.json', 'servers'],
    ['claude', '.mcp.json', 'mcpServers'],
    ['gemini', '.gemini/settings.json', 'mcpServers'],
  ]) {
    assert.equal(w.run(agentId).applied, true);
    const entry = JSON.parse(fs.readFileSync(path.join(w.root, relative)))[mapKey]['workspace-tgrep'];
    assert.equal(Object.hasOwn(entry, 'enabled'), false);
    assert.equal(Object.hasOwn(entry, 'disabled'), false);
  }
});

test('Cline IDE upgrades an old generated snippet but rejects an edited activation flag before writes', t => {
  const w = workspace(t);
  const relative = 'build/integration/mcp/exports/cline-ide-mcp-servers.json';
  const file = w.write(relative, `${JSON.stringify({ mcpServers: { 'workspace-tgrep': w.entry } }, null, 2)}\n`);
  const options = { ideExports: [{ agentId: 'cline-ide', exportsDir: path.dirname(file), backend: 'tgrep' }] };
  assert.equal(w.run('claude', options).applied, true);
  const fresh = fs.readFileSync(file, 'utf8');
  assert.equal(JSON.parse(fresh).mcpServers['workspace-tgrep'].disabled, false);
  const mtime = fs.statSync(file).mtimeMs;
  assert.equal(w.run('claude', options).applied, true);
  assert.equal(fs.statSync(file).mtimeMs, mtime);
  const edited = fresh.replace('"disabled": false', '"disabled": true');
  w.write(relative, edited);
  assert.equal(w.run('trae', options).applied, false);
  assert.equal(fs.readFileSync(file, 'utf8'), edited);
  assert.equal(fs.existsSync(path.join(w.root, '.trae/mcp.json')), false);
});

test('default API writes the actual toolkit path and an explicit workspace', t => {
  const w = workspace(t);
  assert.equal(w.run('trae').applied, true);
  const entry = JSON.parse(fs.readFileSync(path.join(w.root, '.trae/mcp.json'))).mcpServers['workspace-tgrep'];
  assert.deepEqual(entry, { ...w.entry, command: process.execPath });
});

test('legacy JSON ids migrate without changing user options, comments or foreign servers', t => {
  const w = workspace(t);
  const old = { ...w.entry, args: [...w.entry.args, '--max-context', '7'], env: { KEEP: 'literal' } };
  const text = '\uFEFF' + JSON.stringify({ mcpServers: { 'former-project-tgrep': old, 'company-tgrep': { command: 'uvx', args: ['company'] } } }, null, 2).replace('"env":', '/* preserve this option */ "env":');
  const file = w.write('.mcp.json', text);
  assert.deepEqual(agentStates(w.root).claude.configured, ['former-project-tgrep']);
  assert.equal(w.run('claude').applied, true);
  const after = fs.readFileSync(file, 'utf8');
  const map = parseJsonDocument(after, file).root.mcpServers;
  assert.equal(map['former-project-tgrep'], undefined);
  assert.deepEqual(map['workspace-tgrep'], { ...old, command: process.execPath });
  assert.deepEqual(map['company-tgrep'], { command: 'uvx', args: ['company'] });
  assert.ok(after.startsWith('\uFEFF'));
  assert.match(after, /preserve this option/);
  const mtime = fs.statSync(file).mtimeMs;
  assert.equal(w.run('claude').applied, true);
  assert.equal(fs.readFileSync(file, 'utf8'), after);
  assert.equal(fs.statSync(file).mtimeMs, mtime);
});

test('legacy TOML keys and markers migrate while multiline data and subtables survive', t => {
  const w = workspace(t);
  const file = w.write('.codex/config.toml', [
    '# BEGIN former-mcp (managed by anywhere/tools/setup.js; keep the marker lines)',
    '[mcp_servers."former-tgrep"]', 'command = "node"', `args = ${JSON.stringify(w.entry.args)}`,
    'startup_timeout_sec = 123 # keep', 'description = """literal', '[mcp_servers.former-tgrep]', 'value"""',
    "[mcp_servers.'former-tgrep'.env]", 'KEEP = "yes"', '# END former-mcp',
    '[mcp_servers.company-codegraph]', 'command = "uvx"', 'args = ["company"]', '',
  ].join('\r\n'));
  assert.deepEqual(agentStates(w.root).codex.configured, ['workspace-tgrep']);
  const result = w.run('codex');
  assert.equal(result.applied, true, JSON.stringify(result.plan.problems));
  const after = fs.readFileSync(file, 'utf8');
  assert.match(after, /\[mcp_servers\.workspace-tgrep\.env\]/);
  assert.match(after, /startup_timeout_sec = 123 # keep/);
  assert.match(after, /literal\r\n\[mcp_servers.former-tgrep\]\r\nvalue/);
  assert.match(after, /\[mcp_servers.company-codegraph\]/);
  assert.doesNotMatch(after, /BEGIN former-mcp/);
  assert.equal(w.run('codex').applied, true);
  assert.equal(fs.readFileSync(file, 'utf8'), after);
});

test('old and new id collisions fail before any target or metadata write', t => {
  const w = workspace(t);
  const text = JSON.stringify({ mcpServers: { 'former-tgrep': w.entry, 'workspace-tgrep': { ...w.entry, env: { KEEP: 'different' } } } });
  const file = w.write('.mcp.json', text);
  const result = w.run('claude', { operations: [{ type: 'configure', agentId: 'trae', backend: 'tgrep' }, { type: 'configure', agentId: 'claude', backend: 'tgrep' }] });
  assert.equal(result.applied, false);
  assert.equal(fs.readFileSync(file, 'utf8'), text);
  assert.equal(fs.existsSync(path.join(w.root, '.trae')), false);
  assert.equal(fs.existsSync(path.join(w.root, 'build')), false);
});

test('a legacy suffix must match the configured backend before migration claims it', t => {
  const w = workspace(t);
  const json = w.write('.mcp.json', JSON.stringify({ mcpServers: { 'custom-codegraph': w.entry } }));
  const foreignToml = `[mcp_servers.custom-codegraph]\ncommand = "node"\nargs = ${JSON.stringify(w.entry.args)}\n`;
  const toml = w.write('.codex/config.toml', foreignToml);
  assert.equal(w.run('claude').applied, true);
  assert.equal(w.run('codex').applied, true);
  assert.deepEqual(JSON.parse(fs.readFileSync(json)).mcpServers['custom-codegraph'], w.entry);
  assert.ok(fs.readFileSync(toml, 'utf8').includes(foreignToml));
  const spec = GUIDED_IMPORTS['jetbrains-ai'];
  const exportsDir = path.join(w.root, 'build/integration/mcp/exports');
  w.write(path.relative(w.root, path.join(exportsDir, spec.snippetFile)), `${JSON.stringify({ mcpServers: { 'custom-codegraph': w.entry } }, null, 2)}\n`);
  assert.match(ideExportStates(w.root, exportsDir)['jetbrains-ai'].error, /已修改/);
});

test('Cline legacy exports retain user servers when switching to the generic filename', t => {
  const w = workspace(t);
  const old = w.write('.cline/former-mcp.json', JSON.stringify({ mcpServers: { 'former-tgrep': w.entry, company: { command: 'company-tool' } } }));
  assert.deepEqual(agentStates(w.root).cline.configured, ['former-tgrep']);
  const result = w.run('cline');
  assert.equal(result.applied, true, JSON.stringify(result.plan.problems));
  const active = JSON.parse(fs.readFileSync(path.join(w.root, '.cline/mcp.json'))).mcpServers;
  assert.deepEqual(active.company, { command: 'company-tool' });
  assert.deepEqual(active['workspace-tgrep'], { ...w.entry, command: process.execPath, disabled: false });
  assert.deepEqual(JSON.parse(fs.readFileSync(old)).mcpServers, { company: { command: 'company-tool' } });
});

test('generated legacy JSON and Cordis exports are recognized and replaced transactionally', t => {
  const w = workspace(t);
  const exportsDir = path.join(w.root, 'build/integration/mcp/exports');
  for (const agentId of ['jetbrains-ai', 'dsh']) {
    const spec = GUIDED_IMPORTS[agentId];
    const old = spec.render ? spec.render({ entry: w.entry, serverId: 'former-tgrep', repoRoot: w.root })
      : `${JSON.stringify({ mcpServers: { 'former-tgrep': w.entry } }, null, 2)}\n`;
    const file = w.write(path.relative(w.root, path.join(exportsDir, spec.snippetFile)), old);
    assert.deepEqual(ideExportStates(w.root, exportsDir)[agentId].configured, ['workspace-tgrep']);
    const result = w.run('trae', { ideExports: [{ exportsDir, agentId, backend: 'tgrep' }] });
    assert.equal(result.applied, true);
    assert.equal(fs.readFileSync(file, 'utf8'), ideExportSnippetText(w.root, 'tgrep', agentId));
  }
});

test('managed root arguments do not block switching a plain legacy export to another backend', t => {
  const w = workspace(t);
  w.write('.cline/former-mcp.json', JSON.stringify({ mcpServers: { 'former-tgrep': w.entry } }));
  const result = w.run('cline', { operations: [{ type: 'configure', agentId: 'cline', backend: 'codegraph' }] });
  assert.equal(result.applied, true, JSON.stringify(result.plan.problems));
  const active = JSON.parse(fs.readFileSync(path.join(w.root, '.cline/mcp.json'))).mcpServers;
  assert.deepEqual(Object.keys(active), ['workspace-codegraph']);
  assert.deepEqual(active['workspace-codegraph'].args, [path.join(INTEGRATION_ROOT, 'codegraph/src/server.mjs'), '--repo-root', w.root]);
});
