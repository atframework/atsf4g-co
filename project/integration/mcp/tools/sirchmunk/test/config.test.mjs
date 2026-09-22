import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { WorkspacePaths, INTEGRATION_ROOT } from '../../../common/src/paths.mjs';
import { collectConfig, readConfig, writeConfig, configPath, validateConfig } from '../src/config.mjs';
import { configureAgent } from '../../../agents/src/writers.mjs';

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-sirchmunk-config-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  return new WorkspacePaths(root, path.join(root, 'build'));
}

test('Sirchmunk requires all LLM settings and keeps secrets out of generated Agent configs', async t => {
  const paths = fixture(t);
  await assert.rejects(collectConfig(paths, { options: {}, ui: { interactive: false }, env: {} }), /nonempty baseURL/);
  const config = { baseURL: 'https://llm.example.test/v1', apiKey: 'secret-only-local', model: 'test-model' };
  const prompts = [];
  const answers = Object.values(config);
  const collected = await collectConfig(paths, { options: {}, env: {}, ui: { interactive: true, menu: { textInput: async (prompt, options) => { prompts.push({ prompt, options }); return answers.shift(); } } } });
  assert.deepEqual(collected, config);
  assert.equal(prompts.length, 3);
  assert.equal(prompts[1].options.secret, true);
  writeConfig(paths, collected);
  assert.deepEqual(readConfig(paths), config);
  const modified = fs.statSync(configPath(paths)).mtimeMs;
  writeConfig(paths, config);
  assert.equal(fs.statSync(configPath(paths)).mtimeMs, modified);
  for (const agentId of ['claude', 'codex', 'vscode', 'kilo', 'qwen', 'zed']) configureAgent({ repoRoot: paths.repoRoot, agentId, backend: 'sirchmunk', launch: { integrationRoot: INTEGRATION_ROOT } });
  for (const relative of ['.mcp.json', '.codex/config.toml', '.vscode/mcp.json', '.kilo/kilo.jsonc', '.qwen/settings.json', '.zed/settings.json']) {
    const content = fs.readFileSync(path.join(paths.repoRoot, relative), 'utf8');
    assert.match(content, /workspace-sirchmunk/);
    assert.equal(content.includes(config.apiKey), false);
    assert.equal(content.includes(config.baseURL), false);
  }
});

test('invalid private JSON and invalid endpoints fail without revealing the API key', t => {
  const paths = fixture(t);
  assert.throws(() => validateConfig({ baseURL: 'https://user:secret@example.test', apiKey: 'private-key', model: 'x' }), error => !error.message.includes('private-key') && /without credentials/.test(error.message));
  fs.mkdirSync(paths.privateDir, { recursive: true });
  fs.writeFileSync(configPath(paths), '{"apiKey":"do-not-print", bad');
  assert.throws(() => readConfig(paths), error => /invalid/.test(error.message) && !error.message.includes('do-not-print'));
});

test('Sirchmunk CLI dry-run needs no credentials and never creates a download/private directory', t => {
  const paths = fixture(t);
  const result = spawnSync(process.execPath, [path.join(INTEGRATION_ROOT, 'setup.js'), '--backend=sirchmunk', '--agents=claude', '--yes', '--dry-run', '--repo-root=' + paths.repoRoot], { encoding: 'utf8' });
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.match(result.stdout, /sirchmunk/);
  assert.deepEqual(fs.readdirSync(paths.repoRoot), []);
});
