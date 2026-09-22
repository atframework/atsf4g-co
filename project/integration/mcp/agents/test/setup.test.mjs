import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import test from 'node:test';

import { TARGETS, agentDefinitions, targetFor } from '../src/registry.mjs';
import { agentStates } from '../src/writers.mjs';
import { createFileStore, findOpenJournalBatch } from '../src/fileStore.mjs';
import { parseJsonDocument } from '../src/formats/jsonDocument.mjs';

const source = fileURLToPath(new URL('../../', import.meta.url));
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-setup-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const integration = path.join(root, 'tools/mcp');
  fs.mkdirSync(integration, { recursive: true });
  for (const file of ['setup.js']) fs.copyFileSync(path.join(source, file), path.join(integration, file));
  for (const component of ['agents', 'common']) {
    fs.cpSync(path.join(source, component), path.join(integration, component), {
      recursive: true, filter: (file) => !['node_modules', 'test'].includes(path.basename(file)),
    });
  }
  fs.writeFileSync(path.join(root, 'package.json'), '{"type":"module"}');
  const user = path.join(root, 'user');
  fs.mkdirSync(user);
  fs.writeFileSync(path.join(user, 'sentinel'), 'do not change');
  const run = (args) => spawnSync(process.execPath, [path.join(integration, 'setup.js'), ...args], {
    cwd: root, encoding: 'utf8', timeout: 10000,
    env: { ...process.env, HOME: user, USERPROFILE: user, CODEX_HOME: path.join(user, '.codex'), CLINE_BIN_PATH: '', npm_config_prefix: path.join(user, 'npm-prefix') },
  });
  return { root, integration, run };
}

function snapshot(root) {
  const files = {};
  function visit(dir) {
    for (const item of fs.readdirSync(dir, { withFileTypes: true })) {
      const file = path.join(dir, item.name);
      if (item.isDirectory()) visit(file);
      else files[path.relative(root, file)] = createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    }
  }
  visit(root);
  return files;
}

test('review: CLI recovers before scanning defaults and before the no-agents early return', (t) => {
  const { root, run } = fixture(t);
  const installed = run(['--yes', '--skip-prepare', '--backend=tgrep', '--agents=cursor']);
  assert.equal(installed.status, 0, installed.stderr + installed.stdout);
  const file = path.join(root, '.cursor/mcp.json');
  const before = fs.readFileSync(file, 'utf8');
  const dirs = {
    repoRoot: root,
    stateDir: path.join(root, 'build/integration/mcp/state'),
    tmpDir: path.join(root, 'build/_agent_tmp/mcp'),
  };
  const store = createFileStore(dirs);
  store.remove(file, { relative: '.cursor/mcp.json', expectedBefore: before });
  const pid = spawnSync(process.execPath, ['-e', '']).pid;
  const journal = path.join(dirs.tmpDir, 'agent-config-journal.jsonl');
  fs.writeFileSync(journal, fs.readFileSync(journal, 'utf8').replaceAll(`"pid":${process.pid}`, `"pid":${pid}`));

  const preDryRun = snapshot(root);
  const invalid = run(['--yes', '--uninstall']);
  assert.equal(invalid.status, 1);
  assert.match(invalid.stderr + invalid.stdout, /需要 --agents/);
  assert.deepEqual(snapshot(root), preDryRun, 'invalid arguments must not trigger recovery');
  const dryRun = run(['--yes', '--dry-run', '--backend=tgrep']);
  assert.equal(dryRun.status, 0, dryRun.stderr + dryRun.stdout);
  assert.match(dryRun.stdout, /尚未恢复/);
  assert.deepEqual(snapshot(root), preDryRun);

  const resumed = run(['--yes', '--skip-prepare', '--backend=tgrep']);
  assert.equal(resumed.status, 0, resumed.stderr + resumed.stdout);
  assert.match(resumed.stdout, /已还原/);
  assert.equal(fs.readFileSync(file, 'utf8'), before);
  assert.equal(findOpenJournalBatch(dirs), null);
});

test('CLI installs all targets, repeats without writes, switches twice and uninstalls', (t) => {
  const { root, run } = fixture(t);
  const install = (backend) => {
    const result = run(['--yes', '--skip-prepare', `--backend=${backend}`, '--agents=all']);
    assert.equal(result.status, 0, result.stderr + result.stdout);
    for (const agent of agentDefinitions()) {
      if (!targetFor(agent)) continue;
      assert.deepEqual(agentStates(root)[agent.id].configured, [`workspace-${backend}`]);
    }
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
    for (const agent of agentDefinitions()) {
      const target = targetFor(agent);
      if (!target) continue;
      assert.equal(fs.existsSync(path.join(root, target.file)), false);
    }
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

test('CLI help and list-agents stay consistent with the registry', (t) => {
  const { run } = fixture(t);

  // Registry invariants: every product points at a real target, every target
  // serves at least one product, and aliases resolve back to their product.
  for (const agent of agentDefinitions()) {
    assert.ok(!agent.targetId || Object.hasOwn(TARGETS, agent.targetId), `${agent.id} has a real target`);
    assert.equal(targetFor(agent)?.file ?? null, agent.targetId ? TARGETS[agent.targetId].file : null);
  }
  for (const targetId of Object.keys(TARGETS)) {
    assert.ok(agentDefinitions().some((agent) => agent.targetId === targetId), `${targetId} is reachable from the registry`);
  }

  // --list-agents prints exactly the registry ids, each with its real file
  // location (or the guided-import marker for products without a target).
  const list = run(['--list-agents']);
  assert.equal(list.status, 0, list.stderr + list.stdout);
  const listedIds = [...list.stdout.matchAll(/^  (\S+)(?=\s)/gm)].map((match) => match[1]);
  assert.deepEqual(listedIds, agentDefinitions().map((agent) => agent.id));
  for (const agent of agentDefinitions()) {
    const target = targetFor(agent);
    const location = target ? target.file.split(path.sep).join('/') : '面板导入 / 引导（不写项目配置）';
    assert.ok(list.stdout.includes(`(${location})`), `${agent.id} lists ${location}`);
  }

  // --help documents every flag parseArgv accepts (checked here against the
  // flag surface of setup.js; extend this list when adding an option).
  const help = run(['--help']);
  assert.equal(help.status, 0, help.stderr + help.stdout);
  for (const flag of ['--repo-root=', '--build-dir=', '--backend=', '--agents=', '--mirror=', '--npm-mirror=', '--cargo-mirror=', '--tgrep-bin=', '--codegraph-path=', '--offline', '--skip-prepare', '--uninstall', '--all-agents', '--yes', '--ui=line', '--dry-run', '--help', '-h', '--list-agents', '--list-mirrors']) {
    assert.ok(help.stdout.includes(flag), `help documents ${flag}`);
  }
  const documentedFlags = [...help.stdout.matchAll(/^\s{2}(--[a-z-]+)/gm)].map((match) => match[1]);
  assert.deepEqual(documentedFlags, ['--repo-root', '--build-dir', '--backend', '--agents', '--mirror', '--npm-mirror', '--cargo-mirror', '--tgrep-bin', '--codegraph-path', '--offline', '--skip-prepare', '--uninstall', '--all-agents', '--yes', '--ui', '--dry-run', '--help', '--list-agents', '--list-mirrors'], 'help lists exactly the accepted options');
});

test('CLI mirror choices are validated and listing/dry-run never prepares tools', t => {
  const { root, run } = fixture(t);
  const before = snapshot(root);
  const listed = run(['--list-mirrors']);
  assert.equal(listed.status, 0, listed.stderr);
  assert.match(listed.stdout, /npmmirror/);
  assert.match(listed.stdout, /tuna/);
  for (const flag of ['--npm-mirror=missing', '--cargo-mirror=missing', '--npm-mirror=']) {
    const result = run([flag]);
    assert.equal(result.status, 1);
  }
  const planned = run(['--dry-run', '--yes', '--backend=codegraph', '--agents=trae', '--mirror=cn', '--npm-mirror=huawei', '--codegraph-path=not-present']);
  assert.equal(planned.status, 0, planned.stderr);
  assert.match(planned.stdout, /npm：huawei/);
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

test('CLI agents=all writes the cline export but keeps guided imports out', (t) => {
  const { root, run } = fixture(t);
  const result = run(['--yes', '--skip-prepare', '--backend=tgrep', '--agents=all']);
  assert.equal(result.status, 0, result.stderr + result.stdout);
  assert.equal(fs.existsSync(path.join(root, '.cline', 'mcp.json')), true);
  assert.equal(fs.existsSync(path.join(root, 'build', 'integration', 'mcp', 'exports')), false, 'guided snippets are not part of all');
  assert.match(result.stdout, /launch\.mjs" --agent=cline/);
  assert.match(result.stdout, /未纳入 all/);
  assert.match(result.stdout, /显式接入，不等于已自动接入/);
  assert.doesNotMatch(result.stdout, /cline 已接入/);
  for (const agent of agentDefinitions()) {
    if (!targetFor(agent)) continue;
    assert.deepEqual(agentStates(root)[agent.id].configured, ['workspace-tgrep']);
  }
});

test('CLI cline export installs, repeats unchanged, and uninstalls', (t) => {
  const { root, run } = fixture(t);
  const args = ['--yes', '--skip-prepare', '--backend=tgrep', '--agents=cline'];
  const dry = run([...args, '--dry-run']);
  assert.equal(dry.status, 0, dry.stderr + dry.stdout);
  assert.equal(fs.existsSync(path.join(root, '.cline', 'mcp.json')), false);
  assert.match(dry.stdout, /拟新建：\.cline\/mcp\.json/);

  const installed = run(args);
  assert.equal(installed.status, 0, installed.stderr + installed.stdout);
  const parsed = JSON.parse(fs.readFileSync(path.join(root, '.cline', 'mcp.json'), 'utf8'));
  assert.deepEqual(Object.keys(parsed.mcpServers), ['workspace-tgrep']);
  assert.match(installed.stdout, /显式接入，不等于已自动接入/);

  const before = snapshot(root);
  const repeat = run(args);
  assert.equal(repeat.status, 0, repeat.stderr + repeat.stdout);
  assert.deepEqual(snapshot(root), before);
  assert.match(repeat.stdout, /无变化：\.cline\/mcp\.json/);

  const removed = run(['--yes', '--uninstall', '--agents=cline']);
  assert.equal(removed.status, 0, removed.stderr + removed.stdout);
  assert.equal(fs.existsSync(path.join(root, '.cline', 'mcp.json')), false);
});

test('CLI guided imports write build-dir snippets only and clean up on uninstall', (t) => {
  const { root, run } = fixture(t);
  const exportsDir = path.join(root, 'build', 'integration', 'mcp', 'exports');
  const args = ['--yes', '--skip-prepare', '--backend=codegraph', '--agents=cline-ide,codebuddy-ide'];
  const dry = run([...args, '--dry-run']);
  assert.equal(dry.status, 0, dry.stderr + dry.stdout);
  assert.match(dry.stdout, /拟生成 IDE 导入片段/);
  assert.equal(fs.existsSync(exportsDir), false);

  const installed = run(args);
  assert.equal(installed.status, 0, installed.stderr + installed.stdout);
  assert.deepEqual(fs.readdirSync(exportsDir).sort(), ['cline-ide-mcp-servers.json', 'codebuddy-ide-mcp-servers.json']);
  for (const file of fs.readdirSync(exportsDir)) {
    const parsed = JSON.parse(fs.readFileSync(path.join(exportsDir, file), 'utf8'));
    assert.deepEqual(Object.keys(parsed.mcpServers), ['workspace-codegraph']);
    assert.equal(parsed.mcpServers['workspace-codegraph'].command, process.execPath);
    assert.match(parsed.mcpServers['workspace-codegraph'].args[0], /codegraph[\\/]src[\\/]server\.mjs$/);
  }
  assert.match(installed.stdout, /手动导入/);
  assert.doesNotMatch(installed.stdout, /已自动接入/);
  assert.equal(fs.existsSync(path.join(root, '.cline')), false, 'guided ids never create repo config files');
  assert.equal(fs.existsSync(path.join(root, '.mcp.json')), false);

  const removed = run(['--yes', '--uninstall', '--agents=cline-ide,codebuddy-ide']);
  assert.equal(removed.status, 0, removed.stderr + removed.stdout);
  assert.deepEqual(fs.readdirSync(exportsDir), []);
});

test('CLI pi selection reports .pi/mcp.json conflicts without touching that file', (t) => {
  const { root, run } = fixture(t);
  fs.mkdirSync(path.join(root, '.pi'), { recursive: true });
  const conflictText = '{"mcpServers":{"workspace-tgrep":{"command":"node","args":["elsewhere"]}}}';
  fs.writeFileSync(path.join(root, '.pi', 'mcp.json'), conflictText);
  const conflict = run(['--yes', '--skip-prepare', '--backend=codegraph', '--agents=pi']);
  assert.equal(conflict.status, 0, conflict.stderr + conflict.stdout);
  assert.match(conflict.stdout, /\.pi\/mcp\.json 中已存在同名条目 workspace-tgrep/);
  assert.equal(fs.readFileSync(path.join(root, '.pi', 'mcp.json'), 'utf8'), conflictText);

  fs.writeFileSync(path.join(root, '.pi', 'mcp.json'), '{ broken');
  const unparsable = run(['--yes', '--skip-prepare', '--backend=codegraph', '--agents=pi']);
  assert.equal(unparsable.status, 0, unparsable.stderr + unparsable.stdout);
  assert.match(unparsable.stdout, /\.pi\/mcp\.json 存在但无法解析/);
});

test('CLI validates --ui values and stays read-only', (t) => {
  const { root, run } = fixture(t);
  const before = snapshot(root);
  const bad = run(['--ui=arrows']);
  assert.equal(bad.status, 1);
  assert.match(bad.stderr, /--ui 仅支持：line/);
  assert.deepEqual(snapshot(root), before);
  const ok = run(['--yes', '--ui=line', '--dry-run', '--skip-prepare', '--backend=tgrep', '--agents=claude']);
  assert.equal(ok.status, 0, ok.stderr + ok.stdout);
  assert.deepEqual(snapshot(root), before);
});

test('noninteractive missing choices fail before preparing dependencies', (t) => {
  const { root, integration, run } = fixture(t);
  fs.writeFileSync(path.join(integration, 'common/src/prepare.mjs'),
    'export function runPrepare() { throw new Error("PREPARE_MUST_NOT_RUN"); }\nexport function writePreparedState() {}\n');
  const before = snapshot(root);
  const result = run(['--backend=tgrep', '--mirror=official']);
  assert.equal(result.status, 1);
  assert.doesNotMatch(result.stdout + result.stderr, /PREPARE_MUST_NOT_RUN/);
  assert.match(result.stderr, /非交互/);
  assert.deepEqual(snapshot(root), before);
});

test('guided exports participate in final selection and dry-run summaries', (t) => {
  const { root, run } = fixture(t);
  const args = ['--yes', '--skip-prepare', '--backend=tgrep', '--agents=cline-ide'];
  const dry = run([...args, '--dry-run']);
  assert.equal(dry.status, 0, dry.stderr);
  assert.doesNotMatch(dry.stdout, /已生成本机导入片段/);
  assert.equal(run(args).status, 0);
  const snippet = path.join(root, 'build/integration/mcp/exports/cline-ide-mcp-servers.json');
  assert.equal(fs.existsSync(snippet), true);
  assert.equal(run(['--yes', '--skip-prepare', '--backend=codegraph']).status, 0);
  assert.ok(JSON.parse(fs.readFileSync(snippet)).mcpServers['workspace-codegraph'], 'default selection keeps existing guided exports');
  assert.equal(run(['--yes', '--skip-prepare', '--backend=tgrep', '--agents=claude']).status, 0);
  assert.equal(fs.existsSync(snippet), false, 'unselected guided export is removed');
});

test('unreadable IDE export aborts config writes during preflight', (t) => {
  const { root, run } = fixture(t);
  const snippet = path.join(root, 'build/integration/mcp/exports/cline-ide-mcp-servers.json');
  fs.mkdirSync(snippet, { recursive: true });
  const before = snapshot(root);
  const result = run(['--yes', '--skip-prepare', '--backend=codegraph', '--agents=claude,cline-ide']);
  assert.equal(result.status, 1);
  assert.deepEqual(snapshot(root), before, 'later export failure cannot leave earlier config and guidance changes');
});

test('CLI previews and applies Kilo consolidation plus legacy options, then reruns without writes', (t) => {
  const { root, run } = fixture(t);
  const native = path.join(root, '.kilo/kilo.jsonc');
  const candidate = path.join(root, '.kilo/kilo.json');
  const legacy = path.join(root, '.kilocode/mcp.json');
  fs.mkdirSync(path.dirname(native), { recursive: true });
  fs.mkdirSync(path.dirname(legacy), { recursive: true });
  fs.writeFileSync(native, '{"$schema":"https://app.kilo.ai/config.json"}\n');
  fs.writeFileSync(candidate, '// keep snapshot setting\n{"snapshot":false}\n');
  fs.writeFileSync(legacy, JSON.stringify({ mcpServers: {
    'workspace-tgrep': { command: 'node', args: [path.join(root, 'tools/mcp/tgrep/src/server.mjs')], env: { KEEP: 'yes' }, disabled: true },
    team: { command: 'team-tool' },
  } }));
  const args = ['--yes', '--skip-prepare', '--backend=tgrep', '--agents=kilocode'];
  const original = snapshot(root);
  const preview = run([...args, '--dry-run']);
  assert.equal(preview.status, 0, preview.stdout + preview.stderr);
  assert.match(preview.stdout, /拟删除冗余候选/);
  assert.deepEqual(snapshot(root), original);
  const applied = run(args);
  assert.equal(applied.status, 0, applied.stdout + applied.stderr);
  const nativeText = fs.readFileSync(native, 'utf8');
  const document = parseJsonDocument(nativeText, native).root;
  assert.equal(document.snapshot, false);
  assert.equal(document.mcp['workspace-tgrep'].enabled, false);
  assert.deepEqual(document.mcp['workspace-tgrep'].environment, { KEEP: 'yes' });
  assert.match(nativeText, /keep snapshot setting/);
  assert.equal(fs.existsSync(candidate), false);
  assert.deepEqual(JSON.parse(fs.readFileSync(legacy, 'utf8')), { mcpServers: { team: { command: 'team-tool' } } });
  const beforeRepeat = snapshot(root);
  const repeated = run(args);
  assert.equal(repeated.status, 0, repeated.stdout + repeated.stderr);
  assert.deepEqual(snapshot(root), beforeRepeat, 'rerun writes neither config nor journal');
});
