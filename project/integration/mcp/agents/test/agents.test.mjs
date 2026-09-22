import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  SERVER_IDS,
  agentDefinitions,
  agentStates,
  configureAgent,
  removeAgentServers,
  runAgentConfigBatch,
} from './fixtures.mjs';

function tmpRepo() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-agents-'));
}

test('configureAgent writes every supported agent shape', () => {
  const repo = tmpRepo();
  try {
    for (const agent of agentDefinitions()) {
      if (!agent.targetId) {
        assert.throws(() => configureAgent({ repoRoot: repo, agentId: agent.id, backend: 'tgrep' }), /no project config file/);
        continue;
      }
      const result = configureAgent({ repoRoot: repo, agentId: agent.id, backend: 'tgrep' });
      assert.ok(fs.existsSync(result.file), `${agent.id} config file exists`);
    }

    const claude = JSON.parse(fs.readFileSync(path.join(repo, '.mcp.json'), 'utf8'));
    const claudeArgs = claude.mcpServers['workspace-tgrep'].args[0];
    assert.ok(claudeArgs.startsWith(repo), 'absolute server path under the repo');
    assert.ok(claudeArgs.endsWith(path.join('tgrep', 'src', 'server.mjs')));
    assert.equal(claude.mcpServers['workspace-tgrep'].command, process.execPath);
    assert.equal(claude.mcpServers['workspace-tgrep'].type, 'stdio');

    const vscode = JSON.parse(fs.readFileSync(path.join(repo, '.vscode', 'mcp.json'), 'utf8'));
    // Visual Studio and VS Code share this file; use the common absolute path
    // form instead of relying on an IDE-specific variable expansion contract.
    assert.equal(vscode.servers['workspace-tgrep'].args[0], path.join(repo, 'tools/mcp/tgrep/src/server.mjs'));

    const codex = fs.readFileSync(path.join(repo, '.codex', 'config.toml'), 'utf8');
    assert.match(codex, /\[mcp_servers\.workspace-tgrep\]/);
    assert.deepEqual(JSON.parse(codex.match(/^args = (.+)$/m)[1]), ['tools/mcp/tgrep/src/server.mjs', '--repo-root', repo]);
    assert.match(codex, /^cwd = /m);

    const opencode = JSON.parse(fs.readFileSync(path.join(repo, 'opencode.json'), 'utf8'));
    assert.equal(opencode.mcp['workspace-tgrep'].type, 'local');
    assert.equal(opencode.mcp['workspace-tgrep'].command[0], process.execPath);
    assert.equal(opencode.mcp['workspace-tgrep'].enabled, true);

    const zcode = JSON.parse(fs.readFileSync(path.join(repo, '.zcode', 'config.json'), 'utf8'));
    assert.equal(zcode.mcp.servers['workspace-tgrep'].command, process.execPath);

    // P9.3 shapes (verified against each vendor's docs).
    const zed = JSON.parse(fs.readFileSync(path.join(repo, '.zed', 'settings.json'), 'utf8'));
    assert.deepEqual(zed.context_servers['workspace-tgrep'], { command: process.execPath, args: [path.join(repo, 'tools', 'mcp', 'tgrep', 'src', 'server.mjs'), '--repo-root', repo], enabled: true });

    const kimi = JSON.parse(fs.readFileSync(path.join(repo, '.kimi-code', 'mcp.json'), 'utf8'));
    assert.equal(kimi.mcpServers['workspace-tgrep'].command, process.execPath);
    assert.equal(kimi.mcpServers['workspace-tgrep'].cwd, repo);
    assert.equal(kimi.mcpServers['workspace-tgrep'].args[0], 'tools/mcp/tgrep/src/server.mjs');

    const qwen = JSON.parse(fs.readFileSync(path.join(repo, '.qwen', 'settings.json'), 'utf8'));
    assert.equal(qwen.mcpServers['workspace-tgrep'].cwd, repo);

    const workbuddy = JSON.parse(fs.readFileSync(path.join(repo, '.workbuddy', 'mcp.json'), 'utf8'));
    assert.equal(workbuddy.mcpServers['workspace-tgrep'].type, 'stdio');
    assert.ok(workbuddy.mcpServers['workspace-tgrep'].args[0].startsWith(repo));

    const mimo = JSON.parse(fs.readFileSync(path.join(repo, '.mimocode', 'mimocode.json'), 'utf8'));
    assert.equal(mimo.mcp['workspace-tgrep'].type, 'local');
    assert.deepEqual(mimo.mcp['workspace-tgrep'].command.slice(0, 1), [process.execPath]);

    const kilo = JSON.parse(fs.readFileSync(path.join(repo, '.kilo', 'kilo.jsonc'), 'utf8'));
    assert.equal(kilo.mcp['workspace-tgrep'].type, 'local');
    assert.deepEqual(kilo.mcp['workspace-tgrep'].command.slice(0, 1), [process.execPath]);

    const roo = JSON.parse(fs.readFileSync(path.join(repo, '.roo', 'mcp.json'), 'utf8'));
    assert.equal(roo.mcpServers['workspace-tgrep'].type, 'stdio');
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('configureAgent preserves user entries and switches backends', () => {
  const repo = tmpRepo();
  try {
    const file = path.join(repo, '.mcp.json');
    fs.writeFileSync(file, JSON.stringify({ mcpServers: { 'user-tool': { command: 'uvx', args: ['something'] } } }, null, 2));

    configureAgent({ repoRoot: repo, agentId: 'claude', backend: 'tgrep' });
    let config = JSON.parse(fs.readFileSync(file, 'utf8'));
    assert.ok(config.mcpServers['user-tool'], 'user entry preserved');
    assert.ok(config.mcpServers[SERVER_IDS.tgrep]);

    // Switching backends replaces the managed entry and keeps the user's.
    configureAgent({ repoRoot: repo, agentId: 'claude', backend: 'codegraph' });
    config = JSON.parse(fs.readFileSync(file, 'utf8'));
    assert.ok(config.mcpServers['user-tool']);
    assert.equal(config.mcpServers[SERVER_IDS.tgrep], undefined);
    assert.ok(config.mcpServers[SERVER_IDS.codegraph]);

    const states = agentStates(repo);
    assert.deepEqual(states.claude.configured, [SERVER_IDS.codegraph]);
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('JSONC configs are edited with comments, foreign entries, and style preserved', () => {
  const repo = tmpRepo();
  try {
    const file = path.join(repo, '.mcp.json');
    const original = '{\n  // my tools\n  "mcpServers": {\n    "user-tool": { "command": "uvx" }, // trailing note\n  },\n  "model": "gpt-5"\n}\n';
    fs.writeFileSync(file, original, 'utf8');

    configureAgent({ repoRoot: repo, agentId: 'claude', backend: 'tgrep' });
    const edited = fs.readFileSync(file, 'utf8');
    assert.match(edited, /\/\/ my tools/, 'comment preserved');
    assert.match(edited, /trailing note/, 'inline comment preserved');
    const config = JSON.parse(edited.replace(/\/\/[^\n\r]*/g, '').replace(/,(\s*[}\]])/g, '$1'));
    assert.ok(config.mcpServers['user-tool'], 'user entry preserved');
    assert.ok(config.mcpServers[SERVER_IDS.tgrep]);
    assert.equal(config.model, 'gpt-5');

    // Uninstall removes the managed entry and keeps everything else.
    const result = removeAgentServers({ repoRoot: repo, agentId: 'claude' });
    assert.equal(result.action, 'updated');
    const after = fs.readFileSync(file, 'utf8');
    assert.match(after, /\/\/ my tools/);
    const afterConfig = JSON.parse(after.replace(/\/\/[^\n\r]*/g, '').replace(/,(\s*[}\]])/g, '$1'));
    assert.ok(afterConfig.mcpServers['user-tool']);
    assert.equal(afterConfig.mcpServers[SERVER_IDS.tgrep], undefined);
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('codex config keeps foreign TOML tables through add and uninstall', () => {
  const repo = tmpRepo();
  const file = path.join(repo, '.codex', 'config.toml');
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, '# my settings\nmodel = "gpt-5"\n\n[mcp_servers.other]\ncommand = "uvx"\nargs = ["other"]\n');
  try {
    configureAgent({ repoRoot: repo, agentId: 'codex', backend: 'tgrep' });
    let text = fs.readFileSync(file, 'utf8');
    assert.match(text, /^model = "gpt-5"$/m);
    assert.match(text, /\[mcp_servers\.other\]/);
    assert.match(text, /\[mcp_servers\.workspace-tgrep\]/);

    // A second run is idempotent in content (one managed block only).
    configureAgent({ repoRoot: repo, agentId: 'codex', backend: 'tgrep' });
    text = fs.readFileSync(file, 'utf8');
    assert.equal(text.match(/BEGIN workspace-mcp/g).length, 1);

    const result = removeAgentServers({ repoRoot: repo, agentId: 'codex' });
    assert.equal(result.action, 'updated');
    text = fs.readFileSync(file, 'utf8');
    assert.doesNotMatch(text, /workspace/);
    assert.match(text, /\[mcp_servers\.other\]/);
    assert.match(text, /^model = "gpt-5"$/m);
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('removeAgentServers deletes skeleton-only files and preserves real ones', () => {
  const repo = tmpRepo();
  try {
    // File we fully own: removed entirely.
    configureAgent({ repoRoot: repo, agentId: 'cursor', backend: 'codegraph' });
    assert.ok(fs.existsSync(path.join(repo, '.cursor', 'mcp.json')));
    let result = removeAgentServers({ repoRoot: repo, agentId: 'cursor' });
    assert.equal(result.action, 'removed-file');
    assert.equal(fs.existsSync(path.join(repo, '.cursor', 'mcp.json')), false);

    // User-owned gemini settings: file kept even if mcpServers ends up empty.
    const geminiFile = path.join(repo, '.gemini', 'settings.json');
    fs.mkdirSync(path.dirname(geminiFile), { recursive: true });
    fs.writeFileSync(geminiFile, JSON.stringify({ mcpServers: {}, theme: 'dark' }, null, 2));
    configureAgent({ repoRoot: repo, agentId: 'gemini', backend: 'tgrep' });
    result = removeAgentServers({ repoRoot: repo, agentId: 'gemini' });
    assert.equal(result.action, 'updated');
    const gemini = JSON.parse(fs.readFileSync(geminiFile, 'utf8'));
    assert.deepEqual(gemini, { mcpServers: {}, theme: 'dark' });

    // A PRE-EXISTING empty object is not ours: uninstall keeps the file.
    const ompFile = path.join(repo, '.omp', 'mcp.json');
    fs.mkdirSync(path.dirname(ompFile), { recursive: true });
    fs.writeFileSync(ompFile, '{\n  "mcpServers": {}\n}\n', 'utf8');
    configureAgent({ repoRoot: repo, agentId: 'omp', backend: 'tgrep' });
    result = removeAgentServers({ repoRoot: repo, agentId: 'omp' });
    assert.equal(result.action, 'updated', 'file kept without an ownership record');
    const omp = JSON.parse(fs.readFileSync(ompFile, 'utf8'));
    assert.deepEqual(omp, { mcpServers: {} });

    // Removing from an unconfigured agent is a no-op.
    result = removeAgentServers({ repoRoot: repo, agentId: 'zcode' });
    assert.equal(result.action, 'unchanged');
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('dryRun writes nothing', () => {
  const repo = tmpRepo();
  try {
    configureAgent({ repoRoot: repo, agentId: 'claude', backend: 'tgrep', dryRun: true });
    assert.equal(fs.existsSync(path.join(repo, '.mcp.json')), false);
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

// -- damaged configs must never be clobbered ----------------------------------------------------
// JSONC with comments or trailing commas is VALID input for the editors (it is edited, not
// refused — see the JSONC test above); the cases below stay damaged.

const DAMAGED_JSON_CASES = [
  ['invalid json', '{ "mcpServers": '],
  ['root is an array', '[{ "command": "uvx" }]\n'],
  ['root is a string', '"not a config"\n'],
  ['root is empty text', '\n'],
  ['duplicate keys', '{ "mcpServers": {}, "mcpServers": { "user-tool": { "command": "uvx" } } }\n'],
];

for (const [name, content] of DAMAGED_JSON_CASES) {
  test(`configureAgent refuses to overwrite damaged config (${name})`, () => {
    const repo = tmpRepo();
    try {
      const file = path.join(repo, '.mcp.json');
      fs.writeFileSync(file, content, 'utf8');
      assert.throws(() => configureAgent({ repoRoot: repo, agentId: 'claude', backend: 'tgrep' }));
      assert.equal(fs.readFileSync(file, 'utf8'), content, 'file bytes unchanged');
    } finally {
      fs.rmSync(repo, { recursive: true, force: true });
    }
  });

  test(`removeAgentServers refuses damaged config (${name})`, () => {
    const repo = tmpRepo();
    try {
      const file = path.join(repo, '.mcp.json');
      fs.writeFileSync(file, content, 'utf8');
      assert.throws(() => removeAgentServers({ repoRoot: repo, agentId: 'claude' }));
      assert.equal(fs.readFileSync(file, 'utf8'), content, 'file bytes unchanged');
    } finally {
      fs.rmSync(repo, { recursive: true, force: true });
    }
  });
}

test('configureAgent refuses non-object server maps instead of replacing them', () => {
  const repo = tmpRepo();
  try {
    const cases = [
      ['claude', '.mcp.json'],
      ['vscode', path.join('.vscode', 'mcp.json')],
      ['zcode', path.join('.zcode', 'config.json')],
      ['opencode', 'opencode.json'],
    ];
    for (const [agentId, relative] of cases) {
      for (const mapValue of ['[]', 'null', '"x"']) {
        const file = path.join(repo, relative);
        fs.mkdirSync(path.dirname(file), { recursive: true });
        const content = `{\n  "servers": ${mapValue},\n  "mcp": ${mapValue},\n  "mcpServers": ${mapValue},\n  "model": "gpt-5"\n}\n`;
        fs.writeFileSync(file, content, 'utf8');
        assert.throws(() => configureAgent({ repoRoot: repo, agentId, backend: 'tgrep' }), undefined, `${agentId} with ${mapValue}`);
        assert.equal(fs.readFileSync(file, 'utf8'), content, 'file bytes unchanged');
      }
    }
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('agentStates reports damaged configs instead of pretending they are empty', () => {
  const repo = tmpRepo();
  try {
    const file = path.join(repo, '.mcp.json');
    fs.writeFileSync(file, '{ broken', 'utf8');
    const states = agentStates(repo);
    assert.deepEqual(states.claude.configured, []);
    assert.ok(states.claude.error, 'damage is reported');
    // JSONC states are readable (comments tolerated).
    fs.writeFileSync(file, '{ // c\n "mcpServers": { "workspace-tgrep": { "command": "node" } } }\n', 'utf8');
    assert.deepEqual(agentStates(repo).claude.configured, [SERVER_IDS.tgrep]);
    // A healthy sibling is still scanned normally.
    configureAgent({ repoRoot: repo, agentId: 'cursor', backend: 'tgrep' });
    assert.deepEqual(agentStates(repo).cursor.configured, [SERVER_IDS.tgrep]);
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('configureAgent accepts valid JSON with a BOM and preserves it', () => {
  const repo = tmpRepo();
  try {
    const file = path.join(repo, '.mcp.json');
    fs.writeFileSync(file, `\uFEFF${JSON.stringify({ mcpServers: { 'user-tool': { command: 'uvx' } } }, null, 2)}\n`, 'utf8');
    configureAgent({ repoRoot: repo, agentId: 'claude', backend: 'tgrep' });
    const raw = fs.readFileSync(file, 'utf8');
    assert.ok(raw.startsWith('\uFEFF'), 'BOM preserved');
    const config = JSON.parse(raw.replace(/^\uFEFF/, ''));
    assert.ok(config.mcpServers['user-tool'], 'user entry preserved');
    assert.ok(config.mcpServers[SERVER_IDS.tgrep]);
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

// -- legacy marker-less Codex tables must stop at the next real table ---------------------------

test('codex legacy managed table removal stops at the next foreign table', () => {
  const repo = tmpRepo();
  const file = path.join(repo, '.codex', 'config.toml');
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(
    file,
    [
      'model = "gpt-5"',
      '',
      '[mcp_servers.workspace-tgrep]',
      'command = "node"',
      'args = ["tools/mcp/tgrep/src/server.mjs"]',
      '',
      '[mcp_servers.foreign]',
      'command = "uvx"',
      'args = ["other"]',
      '',
      '[other_table]',
      'key = 1',
      '',
    ].join('\n'),
    'utf8',
  );
  try {
    const result = removeAgentServers({ repoRoot: repo, agentId: 'codex' });
    assert.equal(result.action, 'updated');
    const text = fs.readFileSync(file, 'utf8');
    assert.doesNotMatch(text, /workspace/);
    assert.match(text, /\[mcp_servers\.foreign\]/, 'foreign table kept');
    assert.match(text, /\[other_table\]/, 'later foreign table kept');
    assert.match(text, /^model = "gpt-5"$/m);

    // agentStates detects the legacy table before removal and quotes work too.
    fs.writeFileSync(
      file,
      [
        '[mcp_servers."workspace-codegraph"]',
        'command = "node"',
        'args = ["tools/mcp/codegraph/src/server.mjs"]',
        '',
        '[mcp_servers.foreign]',
        'command = "uvx"',
        '',
      ].join('\n'),
      'utf8',
    );
    assert.deepEqual(agentStates(repo).codex.configured, [SERVER_IDS.codegraph]);
    const switched = configureAgent({ repoRoot: repo, agentId: 'codex', backend: 'tgrep' });
    assert.equal(switched.action, 'updated');
    const after = fs.readFileSync(file, 'utf8');
    assert.doesNotMatch(after, /"workspace-codegraph"\]/, 'quoted legacy table removed');
    assert.match(after, /\[mcp_servers\.foreign\]/, 'foreign table kept');
    assert.match(after, /\[mcp_servers\.workspace-tgrep\]/, 'new managed table written');
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('codex nested managed sub-tables are removed with their parent', () => {
  const repo = tmpRepo();
  const file = path.join(repo, '.codex', 'config.toml');
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(
    file,
    [
      '[mcp_servers.workspace-tgrep]',
      'command = "node"',
      'args = ["tools/mcp/tgrep/src/server.mjs"]',
      '',
      '[mcp_servers.workspace-tgrep.env]',
      'FOO = "bar"',
      '',
      '[mcp_servers.keep]',
      'command = "uvx"',
      '',
    ].join('\n'),
    'utf8',
  );
  try {
    removeAgentServers({ repoRoot: repo, agentId: 'codex' });
    const text = fs.readFileSync(file, 'utf8');
    assert.doesNotMatch(text, /workspace/);
    assert.doesNotMatch(text, /FOO/);
    assert.match(text, /\[mcp_servers\.keep\]/);
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('codex table-header-like lines inside multiline strings are not boundaries', () => {
  const repo = tmpRepo();
  const file = path.join(repo, '.codex', 'config.toml');
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(
    file,
    [
      '[mcp_servers.workspace-tgrep]',
      'command = "node"',
      'args = ["tools/mcp/tgrep/src/server.mjs"]',
      'note = """this doc mentions',
      '[mcp_servers.foreign]',
      'inside a string"""',
      '',
      '[mcp_servers.real-foreign]',
      'command = "uvx"',
      '',
      '[mcp_servers.workspace-codegraph]',
      'command = "node"',
      'args = ["tools/mcp/codegraph/src/server.mjs"]',
      "literal = '''another",
      '[mcp_servers.also-fake]',
      "line'''",
      '',
    ].join('\n'),
    'utf8',
  );
  try {
    removeAgentServers({ repoRoot: repo, agentId: 'codex' });
    const text = fs.readFileSync(file, 'utf8');
    assert.doesNotMatch(text, /workspace/, 'both managed tables removed');
    assert.match(text, /\[mcp_servers\.real-foreign\]/, 'real table after the string kept');
    assert.doesNotMatch(text, /inside a string/, 'managed table content (string included) dropped');
    assert.doesNotMatch(text, /\[mcp_servers\.foreign\]/, 'header inside the string was not mistaken for a boundary');
    assert.doesNotMatch(text, /another/, 'literal multiline content dropped');
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

test('codex unterminated or overlapping managed markers abort without writing', () => {
  const repo = tmpRepo();
  const file = path.join(repo, '.codex', 'config.toml');
  fs.mkdirSync(path.dirname(file), { recursive: true });
  try {
    for (const content of [
      '# BEGIN workspace-mcp (managed by tools/mcp/setup.js; keep the marker lines)\n[mcp_servers.workspace-tgrep]\ncommand = "node"\n',
      '# BEGIN workspace-mcp (managed by tools/mcp/setup.js; keep the marker lines)\n[mcp_servers.workspace-tgrep]\n# BEGIN workspace-mcp (managed by tools/mcp/setup.js; keep the marker lines)\n# END workspace-mcp\n',
    ]) {
      fs.writeFileSync(file, content, 'utf8');
      assert.throws(() => removeAgentServers({ repoRoot: repo, agentId: 'codex' }));
      assert.throws(() => configureAgent({ repoRoot: repo, agentId: 'codex', backend: 'tgrep' }));
      assert.equal(fs.readFileSync(file, 'utf8'), content, 'file bytes unchanged');
    }
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});

// -- batch behavior ------------------------------------------------------------------------------

test('runAgentConfigBatch with one damaged target writes nothing at all', () => {
  const repo = tmpRepo();
  try {
    const damaged = path.join(repo, '.mcp.json');
    fs.writeFileSync(damaged, '{ broken', 'utf8');
    const batch = runAgentConfigBatch({
      repoRoot: repo,
      operations: [
        { type: 'configure', agentId: 'cursor', backend: 'tgrep' },
        { type: 'configure', agentId: 'claude', backend: 'tgrep' },
      ],
    });
    assert.equal(batch.applied, false);
    assert.equal(batch.plan.problems.length, 1);
    assert.equal(batch.plan.problems[0].targetId, 'claude-mcp-json');
    assert.equal(fs.existsSync(path.join(repo, '.cursor', 'mcp.json')), false, 'healthy target untouched');
    assert.equal(fs.readFileSync(damaged, 'utf8'), '{ broken');
  } finally {
    fs.rmSync(repo, { recursive: true, force: true });
  }
});
