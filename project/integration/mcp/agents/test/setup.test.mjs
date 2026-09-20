import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import test from 'node:test';

import { agentDefinitions, targetFor } from '../src/registry.mjs';
import { agentStates } from '../src/writers.mjs';

const source = fileURLToPath(new URL('../../', import.meta.url));
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-setup-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const integration = path.join(root, 'project/integration/mcp');
  fs.mkdirSync(integration, { recursive: true });
  for (const file of ['setup.js', 'Plan.md']) fs.copyFileSync(path.join(source, file), path.join(integration, file));
  for (const component of ['agents', 'common']) {
    fs.cpSync(path.join(source, component), path.join(integration, component), {
      recursive: true, filter: (file) => !['node_modules', 'test', 'tools'].includes(path.basename(file)),
    });
  }
  fs.writeFileSync(path.join(root, 'package.json'), '{"type":"module"}');
  const user = path.join(root, 'user');
  fs.mkdirSync(user);
  fs.writeFileSync(path.join(user, 'sentinel'), 'do not change');
  const run = (args) => spawnSync(process.execPath, [path.join(integration, 'setup.js'), ...args], {
    cwd: root, encoding: 'utf8', timeout: 10000,
    env: { ...process.env, HOME: user, USERPROFILE: user, CODEX_HOME: path.join(user, '.codex') },
  });
  return { root, integration, run };
}

function snapshot(root) {
  const files = {};
  function visit(dir) {
    for (const item of fs.readdirSync(dir, { withFileTypes: true })) {
      const file = path.join(dir, item.name);
      if (item.isDirectory()) visit(file);
      else files[path.relative(root, file)] = fs.readFileSync(file).toString('base64');
    }
  }
  visit(root);
  return files;
}

test('CLI installs all targets, repeats without writes, switches twice and uninstalls', (t) => {
  const { root, run } = fixture(t);
  const install = (backend) => {
    const result = run(['--yes', '--skip-prepare', `--backend=${backend}`, '--agents=all']);
    assert.equal(result.status, 0, result.stderr + result.stdout);
    for (const agent of agentDefinitions()) assert.deepEqual(agentStates(root)[agent.id].configured, [`atsf4g-${backend}`]);
  };
  install('tgrep');
  const beforeRepeat = snapshot(root);
  install('tgrep');
  assert.deepEqual(snapshot(root), beforeRepeat, 'idempotency includes backup, journal and ownership files');
  install('codegraph');
  install('tgrep');
  for (let i = 0; i < 2; i++) {
    const result = run(['--yes', '--uninstall', '--all-agents']);
    assert.equal(result.status, 0, result.stderr + result.stdout);
    assert.match(result.stdout, /claude \/ pi \/ codebuddy 共用/);
    for (const agent of agentDefinitions()) assert.equal(fs.existsSync(path.join(root, targetFor(agent).file)), false);
  }
});

test('CLI help/list/dry-runs and rejected arguments are read-only and never wait for stdin', (t) => {
  const { root, run } = fixture(t);
  const before = snapshot(root);
  for (const args of [
    ['--help'], ['--list-agents'],
    ['--yes', '--dry-run', '--backend=tgrep', '--agents=all'],
    ['--yes', '--dry-run', '--backend=codegraph', '--agents=kilocode'],
    ['--yes', '--dry-run', '--uninstall', '--all-agents'],
  ]) {
    const result = run(args);
    assert.equal(result.status, 0, result.stderr + result.stdout);
    assert.doesNotMatch(result.stdout, /配置已生成/);
    assert.deepEqual(snapshot(root), before);
  }
  for (const args of [[], ['--agents=,'], ['--agents=   '], ['--agents=unknown'], ['--backend=toString'], ['--mirror=constructor']]) {
    const result = run(args);
    assert.equal(result.error, undefined, 'no timeout/launcher error');
    assert.equal(result.status, 1, result.stderr + result.stdout);
    assert.deepEqual(snapshot(root), before);
  }
});

test('CLI prepare failure occurs before reading damaged agent configs', (t) => {
  const { root, integration, run } = fixture(t);
  fs.writeFileSync(path.join(integration, 'common/src/prepare.mjs'),
    'export function runPrepare() { throw new Error("INJECTED_PREPARE_FAILURE"); }\nexport function writePreparedState() { throw new Error("unexpected write"); }\n');
  fs.writeFileSync(path.join(root, '.mcp.json'), '{ invalid');
  const before = snapshot(root);
  const result = run(['--yes', '--backend=tgrep', '--agents=claude', '--mirror=official']);
  assert.equal(result.status, 1);
  assert.match(result.stdout, /INJECTED_PREPARE_FAILURE/);
  assert.doesNotMatch(result.stdout + result.stderr, /cannot parse as JSON/);
  assert.deepEqual(snapshot(root), before);
});

test('CLI cannot silently succeed when default selection encounters damaged configs', (t) => {
  const { root, run } = fixture(t);
  fs.mkdirSync(path.join(root, '.roo'));
  fs.writeFileSync(path.join(root, '.roo/mcp.json'), '{ invalid');
  const before = snapshot(root);
  const result = run(['--yes', '--skip-prepare', '--backend=tgrep']);
  assert.equal(result.status, 1);
  assert.match(result.stderr, /roo:/);
  assert.deepEqual(snapshot(root), before);
});

test('CLI CodeGraph flow patches only root AGENTS.md and equivalent reruns write nothing', (t) => {
  const { root, run } = fixture(t);
  const guide = path.join(root, 'AGENTS.md');
  const bridge = path.join(root, 'CLAUDE.md');
  fs.writeFileSync(guide, '# 用户规则\r\n');
  fs.writeFileSync(bridge, '@AGENTS.md\n');
  const args = ['--yes', '--skip-prepare', '--backend=codegraph', '--agents=claude'];
  const before = snapshot(root);
  const dry = run([...args, '--dry-run']);
  assert.equal(dry.status, 0, dry.stderr + dry.stdout);
  assert.match(dry.stdout, /拟更新：AGENTS.md 中的 CodeGraph 提示词/);
  assert.deepEqual(snapshot(root), before);

  const installed = run(args);
  assert.equal(installed.status, 0, installed.stderr + installed.stdout);
  const text = fs.readFileSync(guide, 'utf8');
  assert.match(text, /<!-- CODEGRAPH_START -->/);
  assert.ok(text.startsWith('# 用户规则\r\n'));
  assert.equal(fs.readFileSync(bridge, 'utf8'), '@AGENTS.md\n');
  assert.equal(fs.existsSync(path.join(root, '.codegraph')), false, 'patching does not initialize an index');

  // Change only encoding/newlines, then verify the CLI leaves every byte and
  // all journal/backup records untouched on the next run.
  fs.writeFileSync(guide, Buffer.concat([Buffer.from([0xff, 0xfe]), Buffer.from(text.replace(/\r\n/g, '\n'), 'utf16le')]));
  const equivalent = snapshot(root);
  const repeated = run(args);
  assert.equal(repeated.status, 0, repeated.stderr + repeated.stdout);
  assert.match(repeated.stdout, /无变化：AGENTS.md 中的 CodeGraph 提示词/);
  assert.deepEqual(snapshot(root), equivalent);

  const switched = run(['--yes', '--skip-prepare', '--backend=tgrep', '--agents=claude']);
  assert.equal(switched.status, 0, switched.stderr + switched.stdout);
  const kept = fs.readFileSync(guide);
  assert.equal(run(['--yes', '--uninstall', '--agents=claude']).status, 0);
  assert.deepEqual(fs.readFileSync(guide), kept, 'conditional local-index guidance survives MCP uninstall');
});

test('CLI prepare failure and bad markers never patch guidance or agent configs', (t) => {
  const { root, integration, run } = fixture(t);
  fs.writeFileSync(path.join(root, 'AGENTS.md'), '# rules\n<!-- CODEGRAPH_START -->\n');
  fs.writeFileSync(path.join(integration, 'common/src/prepare.mjs'),
    'export function runPrepare() { throw new Error("GUIDANCE_PREPARE_FAILURE"); }\nexport function writePreparedState() { throw new Error("unexpected write"); }\n');
  const before = snapshot(root);
  const args = ['--yes', '--backend=codegraph', '--agents=claude', '--mirror=official'];
  const failedPrepare = run(args);
  assert.equal(failedPrepare.status, 1);
  assert.match(failedPrepare.stdout, /GUIDANCE_PREPARE_FAILURE/);
  assert.doesNotMatch(failedPrepare.stdout + failedPrepare.stderr, /guidance-marker|unclosed CodeGraph/);
  assert.deepEqual(snapshot(root), before);
  const failedPatch = run([...args, '--skip-prepare']);
  assert.equal(failedPatch.status, 1);
  assert.match(failedPatch.stdout, /codegraph-guidance/);
  assert.equal(fs.existsSync(path.join(root, '.mcp.json')), false);
  assert.deepEqual(snapshot(root), before);
});
