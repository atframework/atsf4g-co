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
} from '../src/agents.mjs';

function tmpRepo() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-agents-'));
}

test('configureAgent writes every supported agent shape', () => {
  const repo = tmpRepo();
  try {
    for (const agent of agentDefinitions()) {
      const result = configureAgent({ repoRoot: repo, agentId: agent.id, backend: 'tgrep' });
      assert.ok(fs.existsSync(result.file), `${agent.id} config file exists`);
    }

    const claude = JSON.parse(fs.readFileSync(path.join(repo, '.mcp.json'), 'utf8'));
    const claudeArgs = claude.mcpServers['atsf4g-tgrep'].args[0];
    assert.ok(claudeArgs.startsWith(repo), 'absolute server path under the repo');
    assert.ok(claudeArgs.endsWith(path.join('tgrep', 'src', 'server.mjs')));
    assert.equal(claude.mcpServers['atsf4g-tgrep'].command, 'node');
    assert.equal(claude.mcpServers['atsf4g-tgrep'].type, 'stdio');

    const vscode = JSON.parse(fs.readFileSync(path.join(repo, '.vscode', 'mcp.json'), 'utf8'));
    assert.match(vscode.servers['atsf4g-tgrep'].args[0], /^\$\{workspaceFolder\}\/project\/integration\/mcp\/tgrep\/src\/server\.mjs$/);

    const codex = fs.readFileSync(path.join(repo, '.codex', 'config.toml'), 'utf8');
    assert.match(codex, /\[mcp_servers\.atsf4g-tgrep\]/);
    assert.match(codex, /^args = \["project\/integration\/mcp\/tgrep\/src\/server\.mjs"\]$/m);
    assert.match(codex, /^cwd = /m);

    const opencode = JSON.parse(fs.readFileSync(path.join(repo, 'opencode.json'), 'utf8'));
    assert.equal(opencode.mcp['atsf4g-tgrep'].type, 'local');
    assert.equal(opencode.mcp['atsf4g-tgrep'].command[0], 'node');
    assert.equal(opencode.mcp['atsf4g-tgrep'].enabled, true);

    const zcode = JSON.parse(fs.readFileSync(path.join(repo, '.zcode', 'config.json'), 'utf8'));
    assert.equal(zcode.mcp.servers['atsf4g-tgrep'].command, 'node');
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
    assert.match(text, /\[mcp_servers\.atsf4g-tgrep\]/);

    // A second run is idempotent in content (one managed block only).
    configureAgent({ repoRoot: repo, agentId: 'codex', backend: 'tgrep' });
    text = fs.readFileSync(file, 'utf8');
    assert.equal(text.match(/BEGIN atsf4g-mcp/g).length, 1);

    const result = removeAgentServers({ repoRoot: repo, agentId: 'codex' });
    assert.equal(result.action, 'updated');
    text = fs.readFileSync(file, 'utf8');
    assert.doesNotMatch(text, /atsf4g/);
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

    // Removing from an unconfigured agent is a no-op.
    result = removeAgentServers({ repoRoot: repo, agentId: 'omp' });
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
