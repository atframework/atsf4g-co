import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { runAgentConfigBatch } from '../src/writers.mjs';
import { isOurServerEntry, serverEntry } from '../src/entries.mjs';
import { ideExportSnippetText } from '../src/guidance/ideExports.mjs';
import { INTEGRATION_ROOT } from '../../common/src/paths.mjs';

const denoArgs = ['run', '--allow-read', '--allow-write', '--allow-env', '--allow-run', '--allow-sys', '--no-prompt', '--no-config', '--no-lock', '--node-modules-dir=manual', '--cached-only'];
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-runtime-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  return root;
}
function runtime(root, kind) {
  return { kind, executable: path.join(root, 'runtime with spaces', kind + (process.platform === 'win32' ? '.exe' : '')) };
}

test('JSON and command-array clients switch runtimes while preserving wrapper options and other servers', t => {
  const root = fixture(t);
  const operations = ['vscode', 'kilo'].map(agentId => ({ type: 'configure', agentId, backend: 'tgrep' }));
  for (const kind of ['node', 'bun', 'deno', 'node']) {
    const selected = runtime(root, kind);
    const result = runAgentConfigBatch({ repoRoot: root, launch: { integrationRoot: INTEGRATION_ROOT, runtime: selected }, operations });
    assert.equal(result.applied, true, JSON.stringify(result.plan.problems));
    for (const [file, field] of [['.vscode/mcp.json', 'servers'], ['.kilo/kilo.jsonc', 'mcp']]) {
      const absolute = path.join(root, file);
      const config = JSON.parse(fs.readFileSync(absolute));
      const entry = config[field]['workspace-tgrep'];
      const argv = Array.isArray(entry.command) ? entry.command : [entry.command, ...entry.args];
      assert.equal(argv[0], selected.executable);
      const prefix = kind === 'deno' ? denoArgs : kind === 'bun' ? ['--no-install', '--no-env-file'] : [];
      assert.deepEqual(argv.slice(1, 1 + prefix.length), prefix);
      assert.ok(argv.includes(path.join(INTEGRATION_ROOT, 'tools/tgrep/src/server.mjs')));
      assert.equal(argv.filter(arg => arg === '--repo-root').length, 1);
      if (kind !== 'node' || entry.env) {
        assert.equal(argv.filter(arg => arg === '--test-option').length, 1);
        assert.deepEqual(config[field].foreign, { command: 'unrelated' });
      } else {
        (Array.isArray(entry.command) ? entry.command : entry.args).push('--test-option');
        entry.env = { CUSTOM: 'preserved' };
        config[field].foreign = { command: 'unrelated' };
        fs.writeFileSync(absolute, JSON.stringify(config));
      }
    }
  }
});

test('Codex migrates multiline legacy args to Deno and back, retaining comments, env and explicit disabled state', t => {
  const root = fixture(t);
  const file = path.join(root, '.codex/config.toml');
  fs.mkdirSync(path.dirname(file));
  const script = path.join(INTEGRATION_ROOT, 'tgrep/src/server.mjs');
  fs.writeFileSync(file, `[mcp_servers.workspace-tgrep]\ncommand = "node" # launch note\nargs = [\n  ${JSON.stringify(script)}, # script note\n  "--repo-root", ${JSON.stringify(root)},\n  "--test-option"\n]\nenabled = false\nstartup_timeout_sec = 90\n[mcp_servers.workspace-tgrep.env]\nCUSTOM = "keep"\n`);
  for (const kind of ['deno', 'bun', 'node']) {
    const options = { repoRoot: root, launch: { integrationRoot: INTEGRATION_ROOT, runtime: runtime(root, kind) }, operations: [{ type: 'configure', agentId: 'codex', backend: 'tgrep' }] };
    const result = runAgentConfigBatch(options);
    assert.equal(result.applied, true, JSON.stringify(result.plan.problems));
    const text = fs.readFileSync(file, 'utf8');
    assert.ok(text.includes(`command = ${JSON.stringify(options.launch.runtime.executable)}`));
    for (const retained of ['# launch note', '# script note', '"--test-option"', 'enabled = false', 'startup_timeout_sec = 90', 'CUSTOM = "keep"']) assert.ok(text.includes(retained), retained);
    assert.equal(text.includes('"run"'), kind === 'deno');
    const repeated = runAgentConfigBatch(options);
    assert.equal(repeated.applied, true);
    assert.equal(fs.readFileSync(file, 'utf8'), text);
  }
});

test('generated JSON and Cordis exports can switch runtimes and uninstall with another runtime', t => {
  const root = fixture(t);
  const exportsDir = path.join(root, 'build/exports');
  const ids = ['copilot-jetbrains', 'dsh'];
  // Use the current product ids rather than a hand-written export file layout.
  for (const kind of ['node', 'deno', 'bun']) {
    const launch = { integrationRoot: INTEGRATION_ROOT, runtime: runtime(root, kind) };
    const result = runAgentConfigBatch({ repoRoot: root, launch, operations: [], ideExports: ids.map(agentId => ({ agentId, exportsDir, backend: 'tgrep' })) });
    assert.equal(result.applied, true, JSON.stringify(result.plan.problems));
    const exports = result.plan.steps.filter(step => step.ideExport);
    assert.equal(exports.length, ids.length);
    for (const step of exports) {
      const text = fs.readFileSync(step.file, 'utf8');
      assert.ok(text.includes(JSON.stringify(launch.runtime.executable)));
      assert.equal(text, ideExportSnippetText(root, 'tgrep', step.ideExport, launch));
    }
  }
  const removed = runAgentConfigBatch({ repoRoot: root, launch: { integrationRoot: INTEGRATION_ROOT }, operations: [], ideExports: ids.map(agentId => ({ agentId, exportsDir })) });
  assert.equal(removed.applied, true, JSON.stringify(removed.plan.problems));
  for (const step of removed.plan.steps.filter(step => step.ideExport)) assert.equal(fs.existsSync(step.file), false);
});

test('Deno ownership requires the managed launch flags and exact script and workspace', t => {
  const root = fixture(t);
  const launch = { integrationRoot: INTEGRATION_ROOT, runtime: runtime(root, 'deno') };
  const entry = { command: launch.runtime.executable, args: [...denoArgs, path.join(INTEGRATION_ROOT, 'tgrep/src/server.mjs'), '--repo-root', root] };
  assert.equal(isOurServerEntry(entry, root, launch), true);
  assert.equal(isOurServerEntry({ ...entry, command: 'unknown-shell' }, root, launch), false);
  assert.equal(isOurServerEntry({ ...entry, args: ['run', '--allow-all', ...entry.args.slice(denoArgs.length)] }, root, launch), false);
  assert.equal(isOurServerEntry({ ...entry, args: [...entry.args.slice(0, -1), path.dirname(root)] }, root, launch), false);
  assert.equal(isOurServerEntry({ ...entry, args: [...denoArgs, path.join(root, 'other.mjs'), '--repo-root', root] }, root, launch), false);
});

test('default generated launch pins the executing runtime executable', t => {
  const entry = serverEntry('servers', fixture(t), 'tgrep');
  assert.equal(entry.command, process.execPath);
});
