import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import test from 'node:test';

const source = fileURLToPath(new URL('../', import.meta.url));

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-launch-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const integration = path.join(root, 'project/integration/mcp');
  fs.mkdirSync(path.join(integration, 'agents/tools'), { recursive: true });
  fs.mkdirSync(path.join(integration, 'common/src'), { recursive: true });
  fs.copyFileSync(path.join(source, 'tools/launch.mjs'), path.join(integration, 'agents/tools/launch.mjs'));
  fs.copyFileSync(path.join(source, '../common/src/paths.mjs'), path.join(integration, 'common/src/paths.mjs'));
  fs.writeFileSync(path.join(integration, 'setup.js'), '// placeholder for root validation\n');
  fs.writeFileSync(path.join(root, 'package.json'), '{"type":"module"}');
  const isolatedEnv = {
    ...process.env,
    HOME: root,
    USERPROFILE: root,
    APPDATA: path.join(root, 'appdata'),
    CLINE_BIN_PATH: '',
    npm_config_prefix: path.join(root, 'npm-prefix'),
  };
  const run = (args, env = {}) => spawnSync(process.execPath, [path.join(integration, 'agents/tools/launch.mjs'), ...args], {
    cwd: root, encoding: 'utf8', timeout: 15000, env: { ...isolatedEnv, ...env },
  });
  return { root, integration, run };
}

/** Mirror of the launcher's probe list; skip the not-found test if a real cline exists. */
function clineGloballyPresent() {
  const candidates = [];
  if (process.env.npm_config_prefix) candidates.push(process.env.npm_config_prefix);
  if (process.env.APPDATA) candidates.push(path.join(process.env.APPDATA, 'npm', 'node_modules'));
  candidates.push(path.join(path.dirname(process.execPath), 'node_modules'));
  candidates.push(path.join(path.dirname(process.execPath), '..', 'lib', 'node_modules'));
  candidates.push('/usr/local/lib/node_modules');
  candidates.push('/opt/homebrew/lib/node_modules');
  return candidates.some((root) => fs.existsSync(path.join(root, 'cline', 'bin', 'cline')));
}

function writeFakeCline(root) {
  const file = path.join(root, 'fake-cline.cjs');
  const report = path.join(root, 'fake-cline-report.json');
  fs.writeFileSync(file, [
    'const fs = require("node:fs");',
    `fs.writeFileSync(${JSON.stringify(report)}, JSON.stringify({`,
    '  settingsPath: process.env.CLINE_MCP_SETTINGS_PATH,',
    '  cwd: process.cwd(),',
    '  argv: process.argv.slice(2),',
    '}));',
  ].join('\n'));
  return { file, report };
}

test('launcher help and argument validation', (t) => {
  const { run } = fixture(t);
  const help = run(['--help']);
  assert.equal(help.status, 0, help.stderr);
  assert.match(help.stdout, /--agent=cline/);

  assert.equal(run(['--agent=unknown']).status, 1);
  assert.match(run(['--agent=unknown']).stderr, /仅支持 cline/);
  assert.equal(run([]).status, 1);
  assert.equal(run(['--nonsense']).status, 1);
});

test('launcher requires the exported settings file before starting cline', (t) => {
  const { run } = fixture(t);
  const missing = run(['--agent=cline', '--', '--version']);
  assert.equal(missing.status, 1);
  assert.match(missing.stderr, /atsf4g-mcp\.json/);
  assert.match(missing.stderr, /setup\.js/);
});

test('launcher rejects Windows shell shims and missing explicit entries', (t) => {
  const { root, run } = fixture(t);
  fs.mkdirSync(path.join(root, '.cline'), { recursive: true });
  fs.writeFileSync(path.join(root, '.cline/atsf4g-mcp.json'), '{"mcpServers":{}}\n');
  const shim = run(['--agent=cline', '--cline', path.join(root, 'cline.cmd'), '--', '--version']);
  assert.equal(shim.status, 1);
  assert.match(shim.stderr, /shell shim/);
  const missing = run(['--agent=cline', '--cline', path.join(root, 'absent.exe'), '--', '--version']);
  assert.equal(missing.status, 1);
  assert.match(missing.stderr, /--cline 指向的文件不存在/);
});

test('launcher runs a JS entry with CLINE_MCP_SETTINGS_PATH, repo cwd, and passthrough args', (t) => {
  const { root, run } = fixture(t);
  fs.mkdirSync(path.join(root, '.cline'), { recursive: true });
  fs.writeFileSync(path.join(root, '.cline/atsf4g-mcp.json'), '{"mcpServers":{}}\n');
  const { file, report } = writeFakeCline(root);
  const started = run(['--agent=cline', '--cline', file, '--', '--version', '--extra', 'value with space']);
  assert.equal(started.status, 0, started.stderr);
  const observed = JSON.parse(fs.readFileSync(report, 'utf8'));
  assert.equal(observed.settingsPath, path.join(root, '.cline', 'atsf4g-mcp.json'));
  assert.equal(path.resolve(observed.cwd), path.resolve(root));
  assert.deepEqual(observed.argv, ['--version', '--extra', 'value with space']);
});

test('launcher reports a clear install hint when no cline entry is resolvable', (t) => {
  if (clineGloballyPresent()) {
    t.skip('a real cline installation is present on this machine');
    return;
  }
  const { root, run } = fixture(t);
  fs.mkdirSync(path.join(root, '.cline'), { recursive: true });
  fs.writeFileSync(path.join(root, '.cline/atsf4g-mcp.json'), '{"mcpServers":{}}\n');
  const failed = run(['--agent=cline', '--', '--version']);
  assert.equal(failed.status, 1);
  assert.match(failed.stderr, /npm install -g cline/);
  assert.match(failed.stderr, /--cline/);
});

test('explicit extensionless npm bin/cline runs under Node', (t) => {
  const { root, run } = fixture(t);
  fs.mkdirSync(path.join(root, '.cline'));
  fs.writeFileSync(path.join(root, '.cline/atsf4g-mcp.json'), '{"mcpServers":{}}');
  const { file, report } = writeFakeCline(root);
  const resolver = path.join(root, 'bin', 'cline');
  fs.mkdirSync(path.dirname(resolver));
  fs.writeFileSync(resolver, '#!/usr/bin/env node\n' + fs.readFileSync(file, 'utf8'));
  // npm bin scripts can be ESM or CJS according to the package boundary.
  fs.writeFileSync(path.join(root, 'bin/package.json'), '{"type":"commonjs"}');
  const result = run(['--agent=cline', '--cline', resolver, '--', 'argument with space']);
  assert.equal(result.status, 0, result.stderr);
  assert.deepEqual(JSON.parse(fs.readFileSync(report, 'utf8')).argv, ['argument with space']);
});

test('environment override uses the same entry validation as --cline', (t) => {
  const { root, run } = fixture(t);
  fs.mkdirSync(path.join(root, '.cline'));
  fs.writeFileSync(path.join(root, '.cline/atsf4g-mcp.json'), '{"mcpServers":{}}');
  const { file, report } = writeFakeCline(root);
  const result = run(['--agent=cline', '--', 'ok'], { CLINE_BIN_PATH: file });
  assert.equal(result.status, 0, result.stderr);
  assert.deepEqual(JSON.parse(fs.readFileSync(report, 'utf8')).argv, ['ok']);
  const shim = path.join(root, 'cline.cmd');
  fs.writeFileSync(shim, 'exit 0');
  const rejected = run(['--agent=cline'], { CLINE_BIN_PATH: shim });
  assert.equal(rejected.status, 1);
  assert.match(rejected.stderr, /shell shim/);
  const missing = run(['--agent=cline'], { CLINE_BIN_PATH: path.join(root, 'missing') });
  assert.equal(missing.status, 1);
  assert.match(missing.stderr, /CLINE_BIN_PATH.*不存在/);
});

test('launcher consumes CLINE_BIN_PATH before running an npm resolver', (t) => {
  const { root, run } = fixture(t);
  fs.mkdirSync(path.join(root, '.cline'));
  fs.writeFileSync(path.join(root, '.cline/atsf4g-mcp.json'), '{"mcpServers":{}}');
  const { file, report } = writeFakeCline(root);
  fs.appendFileSync(file, '\nif (process.env.CLINE_BIN_PATH) throw new Error("resolver would launch override again");');
  const explicit = run(['--agent=cline', '--cline', file], { CLINE_BIN_PATH: path.join(root, 'wrong-binary') });
  assert.equal(explicit.status, 0, explicit.stderr);
  const override = run(['--agent=cline'], { CLINE_BIN_PATH: file });
  assert.equal(override.status, 0, override.stderr);
  assert.ok(fs.existsSync(report));
});
