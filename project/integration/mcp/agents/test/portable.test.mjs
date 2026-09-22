import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import test from 'node:test';
import { agentById, SERVER_IDS } from '../src/registry.mjs';
import { runAgentConfigBatch } from '../src/writers.mjs';
import { ideExportSnippetText } from '../src/guidance/ideExports.mjs';
import { parseJsonDocument } from '../src/formats/jsonDocument.mjs';
import { isOurServerEntry } from '../src/entries.mjs';

const integrationRoot = fileURLToPath(new URL('../../', import.meta.url));
function workspace(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-portable-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, '.git'));
  return root;
}

test('setup detects the project above a nested execution directory', (t) => {
  const root = workspace(t);
  const cwd = path.join(root, 'src/deep');
  fs.mkdirSync(cwd, { recursive: true });
  const result = spawnSync(process.execPath, [path.join(integrationRoot, 'setup.js'), '--yes', '--dry-run', '--backend=tgrep', '--agents=trae'], {
    cwd, encoding: 'utf8', timeout: 10000,
  });
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.equal(result.stdout.split(/\r?\n/).find(line => line.startsWith('仓库：')), `仓库：${fs.realpathSync(root)}`);
  assert.deepEqual(fs.readdirSync(root).sort(), ['.git', 'src']);
});

test('Copilot IDE aliases share a target; Rider has an independent delivery', () => {
  for (const id of ['copilot', 'copilot-vscode', 'copilot-visual-studio', 'visual-studio']) {
    assert.equal(agentById(id).id, 'vscode');
  }
  assert.equal(agentById('copilot-rider').id, 'copilot-jetbrains');
  assert.equal(agentById('rider-ai').id, 'jetbrains-ai');
  assert.equal(agentById('trae-cli').id, agentById('trae-ide').id);
});

test('guided clients receive their native formats and a pinned workspace', (t) => {
  const root = workspace(t);
  const launch = { integrationRoot };
  const copilot = JSON.parse(ideExportSnippetText(root, 'tgrep', 'copilot-jetbrains', launch));
  assert.ok(copilot.servers[SERVER_IDS.tgrep]);
  assert.equal(copilot.mcpServers, undefined);
  assert.deepEqual(copilot.servers[SERVER_IDS.tgrep].args.slice(-2), ['--repo-root', root]);
  const jetbrains = JSON.parse(ideExportSnippetText(root, 'tgrep', 'jetbrains-ai', launch));
  assert.ok(jetbrains.mcpServers[SERVER_IDS.tgrep]);
  const dsh = ideExportSnippetText(root, 'tgrep', 'dsh', launch);
  assert.match(dsh, /^- insert:\n/);
  assert.match(dsh, /@deepseek-ai\/dsh-mcp-client/);
  assert.match(dsh, /transport: stdio/);
  assert.match(dsh, /serverName:/);
  assert.equal(dsh.includes('!!js'), false);
});

test('external wrapper paths without an absolute scope are not claimed as this workspace', (t) => {
  const root = workspace(t);
  const script = path.join(integrationRoot, 'tgrep/src/server.mjs');
  const launch = { integrationRoot };
  assert.equal(isOurServerEntry({ command: 'node', args: [script] }, root, launch), false);
  assert.equal(isOurServerEntry({ command: 'node', args: [script, '--repo-root', '.'] }, root, launch), false);
  assert.equal(isOurServerEntry({ command: 'node', args: [script, '--repo-root', root] }, root, launch), true);
});

test('cache root aliases and junction escapes fail before installation writes', (t) => {
  const root = workspace(t);
  const outside = workspace(t);
  const junction = path.join(root, 'cache');
  fs.symlinkSync(outside, junction, process.platform === 'win32' ? 'junction' : 'dir');
  for (const buildDir of [process.platform === 'win32' ? root.toUpperCase() : root, path.join(junction, 'new-cache')]) {
    const result = spawnSync(process.execPath, [path.join(integrationRoot, 'setup.js'), '--yes', '--skip-prepare',
      '--backend=tgrep', '--agents=trae', `--build-dir=${buildDir}`], { cwd: root, encoding: 'utf8', timeout: 10000 });
    assert.equal(result.status, 1, result.stdout + result.stderr);
    assert.match(result.stderr, /构建\/缓存目录必须位于当前工作区之内/);
    assert.deepEqual(fs.readdirSync(outside), ['.git']);
    assert.deepEqual(fs.readdirSync(root), ['.git', 'cache']);
  }
});

test('an external setup configures its cwd and repeats/switches/uninstalls safely', (t) => {
  const root = workspace(t);
  const kit = path.join(workspace(t), 'shared tools/portable bundle');
  fs.mkdirSync(kit, { recursive: true });
  fs.copyFileSync(path.join(integrationRoot, 'setup.js'), path.join(kit, 'setup.js'));
  fs.copyFileSync(path.join(integrationRoot, 'package.json'), path.join(kit, 'package.json'));
  for (const component of ['agents', 'common']) fs.cpSync(path.join(integrationRoot, component), path.join(kit, component), {
    recursive: true, filter: (file) => !['node_modules', 'test'].includes(path.basename(file)),
  });
  const run = (args) => spawnSync(process.execPath, [path.join(kit, 'setup.js'), ...args], {
    cwd: root, encoding: 'utf8', timeout: 10000,
  });
  const args = ['--yes', '--skip-prepare', '--backend=tgrep', '--agents=trae,copilot,jetbrains-ai,dsh'];
  const dry = run([...args, '--dry-run']);
  assert.equal(dry.status, 0, dry.stderr + dry.stdout);
  assert.deepEqual(fs.readdirSync(root), ['.git']);
  const first = run(args);
  assert.equal(first.status, 0, first.stderr + first.stdout);
  const file = path.join(root, '.trae/mcp.json');
  const before = fs.readFileSync(file, 'utf8');
  const entry = JSON.parse(before).mcpServers[SERVER_IDS.tgrep];
  assert.equal(entry.args[0], path.join(kit, 'tgrep/src/server.mjs'));
  assert.deepEqual(entry.args.slice(1), ['--repo-root', root]);
  const mtime = fs.statSync(file).mtimeMs;
  const repeated = run(args);
  assert.equal(repeated.status, 0, repeated.stderr + repeated.stdout);
  assert.equal(fs.readFileSync(file, 'utf8'), before);
  assert.equal(fs.statSync(file).mtimeMs, mtime);
  const switched = run(args.map((arg) => arg === '--backend=tgrep' ? '--backend=codegraph' : arg));
  assert.equal(switched.status, 0, switched.stderr + switched.stdout);
  assert.ok(JSON.parse(fs.readFileSync(file)).mcpServers[SERVER_IDS.codegraph]);
  const removed = run(['--yes', '--uninstall', '--all-agents']);
  assert.equal(removed.status, 0, removed.stderr + removed.stdout);
  assert.equal(fs.existsSync(file), false);
});

test('external JSON, command-array and TOML clients keep scope and optional arguments', (t) => {
  const root = workspace(t);
  const launch = { integrationRoot, buildDir: path.join(root, 'cache with spaces') };
  const operations = ['claude', 'codex', 'opencode', 'qwen'].map((agentId) => ({ type: 'configure', agentId, backend: 'tgrep' }));
  const first = runAgentConfigBatch({ repoRoot: root, launch, operations });
  assert.equal(first.applied, true);
  const file = path.join(root, 'opencode.json');
  const content = JSON.parse(fs.readFileSync(file));
  content.mcp[SERVER_IDS.tgrep].command.push('--test-option');
  content.mcp[SERVER_IDS.tgrep].environment = { CUSTOM_VALUE: 'kept' };
  fs.writeFileSync(file, JSON.stringify(content));
  const repeated = runAgentConfigBatch({ repoRoot: root, launch, operations });
  assert.equal(repeated.applied, true, JSON.stringify(repeated.plan.problems));
  const command = JSON.parse(fs.readFileSync(file)).mcp[SERVER_IDS.tgrep].command;
  assert.deepEqual(command, [process.execPath, path.join(integrationRoot, 'tgrep/src/server.mjs'), '--repo-root', root, '--build-dir', launch.buildDir, '--test-option']);
  assert.deepEqual(JSON.parse(fs.readFileSync(file)).mcp[SERVER_IDS.tgrep].environment, { CUSTOM_VALUE: 'kept' });
  const toml = fs.readFileSync(path.join(root, '.codex/config.toml'), 'utf8');
  assert.ok(toml.includes(JSON.stringify(root)));
  assert.ok(toml.includes('"--repo-root"'));
  assert.ok(toml.includes(JSON.stringify(launch.buildDir)));
});

for (const format of ['json', 'toml']) test(`${format} scope pointing to another workspace aborts the whole batch`, (t) => {
  const root = workspace(t);
  const elsewhere = workspace(t);
  const launch = { integrationRoot };
  const agentId = format === 'json' ? 'trae' : 'codex';
  const operations = [{ type: 'configure', agentId, backend: 'tgrep' }];
  assert.equal(runAgentConfigBatch({ repoRoot: root, launch, operations }).applied, true);
  const file = path.join(root, format === 'json' ? '.trae/mcp.json' : '.codex/config.toml');
  let text = fs.readFileSync(file, 'utf8');
  if (format === 'json') {
    const config = JSON.parse(text);
    config.mcpServers[SERVER_IDS.tgrep].args[2] = elsewhere;
    text = JSON.stringify(config);
  } else {
    text = text.replace(`"--repo-root", ${JSON.stringify(root)}`, `"--repo-root", ${JSON.stringify(elsewhere)}`);
  }
  fs.writeFileSync(file, text);
  const result = runAgentConfigBatch({ repoRoot: root, launch, operations: [...operations, { type: 'configure', agentId: 'cursor', backend: 'tgrep' }] });
  assert.equal(result.applied, false);
  assert.equal(result.plan.problems[0].error.kind, 'server-conflict');
  assert.equal(fs.readFileSync(file, 'utf8'), text);
  assert.equal(fs.existsSync(path.join(root, '.cursor/mcp.json')), false);
});

test('Unreal scan policy preserves custom source selection and is idempotent', (t) => {
  const root = workspace(t);
  fs.mkdirSync(path.join(root, 'Client/Engine/Source'), { recursive: true });
  const file = path.join(root, 'codegraph.json');
  fs.writeFileSync(file, '{\n // team scope\n "include": ["Client/Projects/Game/Source/**"], "exclude": ["private/**"]\n}\n');
  const options = { repoRoot: root, operations: [{ type: 'configure', agentId: 'trae', backend: 'codegraph' }], codegraphGuidance: true };
  const first = runAgentConfigBatch(options);
  assert.equal(first.applied, true, JSON.stringify(first.plan.problems));
  const text = fs.readFileSync(file, 'utf8');
  const config = parseJsonDocument(text, file).root;
  assert.deepEqual(config.include, ['Client/Projects/Game/Source/**']);
  assert.ok(config.exclude.includes('private/**'));
  assert.ok(config.exclude.includes('**/Intermediate/**'));
  assert.ok(config.exclude.includes('**/Content/**'));
  assert.match(text, /\/\/ team scope/);
  const second = runAgentConfigBatch(options);
  assert.equal(second.applied, true);
  assert.ok(second.plan.steps.every((step) => step.action === 'unchanged'));
});

test('malformed Unreal policy and edited DSH patches each block all config writes', (t) => {
  const root = workspace(t);
  fs.writeFileSync(path.join(root, 'Game.uproject'), '{}');
  fs.writeFileSync(path.join(root, 'codegraph.json'), '{"include": false}');
  const options = { repoRoot: root, operations: [{ type: 'configure', agentId: 'trae', backend: 'codegraph' }], codegraphGuidance: true };
  const invalidPolicy = runAgentConfigBatch(options);
  assert.equal(invalidPolicy.applied, false);
  assert.equal(invalidPolicy.plan.problems[0].error.kind, 'invalid-scan-policy');
  assert.equal(fs.existsSync(path.join(root, '.trae/mcp.json')), false);
  const exportsDir = path.join(root, 'build/exports');
  fs.mkdirSync(exportsDir, { recursive: true });
  fs.writeFileSync(path.join(exportsDir, 'dsh-mcp.cordis.yml'), '# user-owned patch\n');
  const invalidExport = runAgentConfigBatch({ ...options, codegraphGuidance: false, ideExports: [{ agentId: 'dsh', exportsDir, backend: 'codegraph' }] });
  assert.equal(invalidExport.applied, false);
  assert.equal(invalidExport.plan.problems[0].error.kind, 'export-conflict');
  assert.equal(fs.existsSync(path.join(root, '.trae/mcp.json')), false);
});

test('malformed TRAE config aborts guided exports in the same batch', (t) => {
  const root = workspace(t);
  fs.mkdirSync(path.join(root, '.trae'));
  fs.writeFileSync(path.join(root, '.trae/mcp.json'), '{ damaged');
  const result = runAgentConfigBatch({
    repoRoot: root, launch: { integrationRoot },
    operations: [{ type: 'configure', agentId: 'trae', backend: 'tgrep' }],
    ideExports: [{ agentId: 'dsh', exportsDir: path.join(root, 'build/exports'), backend: 'tgrep' }],
  });
  assert.equal(result.applied, false);
  assert.ok(result.plan.problems.length);
  assert.equal(fs.readFileSync(path.join(root, '.trae/mcp.json'), 'utf8'), '{ damaged');
  assert.equal(fs.existsSync(path.join(root, 'build/exports/dsh-mcp.cordis.yml')), false);
});
