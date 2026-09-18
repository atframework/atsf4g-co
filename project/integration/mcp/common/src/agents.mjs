/**
 * Agent MCP configuration writers for the atsf4g-co wrappers.
 *
 * Every supported agent gets a PROJECT-LEVEL config file inside the repository
 * (nothing is written outside the repo): setup.js merges our server entry into
 * each selected agent's file and removes it on uninstall, always preserving the
 * user's other entries. Formats and locations follow each agent's current
 * official documentation (see README.md for links); the entries themselves are
 * verified against the researched docs, not guessed.
 *
 * Path strategy: VS Code and Codex officially support ${workspaceFolder} / cwd,
 * so their entries stay portable (commit-friendly). The remaining agents have
 * no documented variable/cwd support, so setup.js writes absolute paths that
 * are resolved on this machine at setup time.
 */

import fs from 'node:fs';
import path from 'node:path';

export const SERVER_IDS = Object.freeze({
  tgrep: 'atsf4g-tgrep',
  codegraph: 'atsf4g-codegraph',
});

export const BACKENDS = Object.freeze({
  tgrep: {
    serverId: SERVER_IDS.tgrep,
    entry: path.join('project', 'integration', 'mcp', 'tgrep', 'src', 'server.mjs'),
    label: 'tgrep — fast text/regex search',
  },
  codegraph: {
    serverId: SERVER_IDS.codegraph,
    entry: path.join('project', 'integration', 'mcp', 'codegraph', 'src', 'server.mjs'),
    label: 'CodeGraph — structural code navigation',
  },
});

/**
 * @typedef {object} AgentDef
 * @property {string} id
 * @property {string} label
 * @property {string} file repository-relative config path
 * @property {'mcpServers'|'vscodeServers'|'zcode'|'opencode'|'codexToml'} kind
 */
const AGENT_LIST = Object.freeze([
  { id: 'claude', label: 'Claude Code (+ pi, shared .mcp.json)', file: '.mcp.json', kind: 'mcpServers' },
  { id: 'codex', label: 'Codex CLI', file: path.join('.codex', 'config.toml'), kind: 'codexToml' },
  { id: 'vscode', label: 'VS Code / GitHub Copilot', file: path.join('.vscode', 'mcp.json'), kind: 'vscodeServers' },
  { id: 'cursor', label: 'Cursor', file: path.join('.cursor', 'mcp.json'), kind: 'mcpServers' },
  { id: 'gemini', label: 'Gemini CLI', file: path.join('.gemini', 'settings.json'), kind: 'mcpServers' },
  { id: 'opencode', label: 'OpenCode', file: 'opencode.json', kind: 'opencode' },
  { id: 'kilocode', label: 'Kilo Code', file: path.join('.kilocode', 'mcp.json'), kind: 'mcpServers' },
  { id: 'roo', label: 'Roo Code', file: path.join('.roo', 'mcp_settings.json'), kind: 'mcpServers' },
  { id: 'zcode', label: 'ZCode', file: path.join('.zcode', 'config.json'), kind: 'zcode' },
  { id: 'omp', label: 'oh-my-pi (omp)', file: path.join('.omp', 'mcp.json'), kind: 'mcpServers' },
]);

export function agentDefinitions() {
  return AGENT_LIST;
}

export function agentById(agentId) {
  return AGENT_LIST.find((agent) => agent.id === agentId) ?? null;
}

function absoluteServerEntry(repoRoot, backend) {
  return path.join(repoRoot, BACKENDS[backend].entry);
}

/** stdio entry for the common `mcpServers`-style agents (absolute paths). */
function mcpServersEntry(repoRoot, backend) {
  return {
    type: 'stdio',
    command: 'node',
    args: [absoluteServerEntry(repoRoot, backend)],
  };
}

/** VS Code `.vscode/mcp.json` server entry: ${workspaceFolder} keeps it commit-friendly. */
function vscodeServerEntry(repoRoot, backend) {
  const relativeEntry = BACKENDS[backend].entry.split(path.sep).join('/');
  return {
    type: 'stdio',
    command: 'node',
    args: ['${workspaceFolder}/' + relativeEntry],
  };
}

/** ZCode `.zcode/config.json` entry under mcp.servers. */
function zcodeServerEntry(repoRoot, backend) {
  return mcpServersEntry(repoRoot, backend);
}

/** OpenCode `opencode.json` entry: type local, command is [command, ...args]. */
function opencodeServerEntry(repoRoot, backend) {
  return {
    type: 'local',
    command: ['node', absoluteServerEntry(repoRoot, backend)],
    enabled: true,
  };
}

const TOML_BEGIN = '# BEGIN atsf4g-mcp (managed by project/integration/mcp/setup.js; keep the marker lines)';

function tomlQuote(value) {
  return `"${String(value).replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
}

/** Codex `.codex/config.toml` section: cwd + repo-relative path (officially supported). */
function codexTomlSection(repoRoot, backend) {
  const lines = [
    TOML_BEGIN,
    `[mcp_servers.${BACKENDS[backend].serverId}]`,
    'command = "node"',
    `args = [${tomlQuote(BACKENDS[backend].entry.split(path.sep).join('/'))}]`,
    `cwd = ${tomlQuote(repoRoot)}`,
    '# END atsf4g-mcp',
  ];
  return lines.join('\n');
}

function removeCodexTomlServers(text) {
  const lines = text.split('\n');
  const kept = [];
  let skipping = false;
  let changed = false;
  for (const line of lines) {
    if (line.trim() === TOML_BEGIN) {
      skipping = true;
      changed = true;
      continue;
    }
    if (skipping) {
      if (line.trim() === '# END atsf4g-mcp') {
        skipping = false;
      }
      continue;
    }
    // Legacy sections without markers: drop whole [mcp_servers.atsf4g-*] tables.
    const table = /^\s*\[([^\]]+)\]\s*$/.exec(line);
    if (table && /^mcp_servers\.(atsf4g-tgrep|atsf4g-codegraph)(\..+)?$/.test(table[1].trim())) {
      skipping = true;
      changed = true;
      continue;
    }
    kept.push(line);
  }
  if (skipping) {
    changed = true; // unterminated block: everything to EOF was ours
  }
  let out = kept.join('\n').replace(/\n{3,}/g, '\n\n');
  if (out.endsWith('\n')) {
    out = `${out.slice(0, -1)}\n`;
  }
  return { text: out, changed };
}

// -- JSON config files ----------------------------------------------------------

const JSON_ROOT_KEYS = {
  mcpServers: 'mcpServers',
  vscodeServers: 'servers',
  zcode: 'mcp',
  opencode: 'mcp',
};

function readJsonObject(filePath) {
  try {
    const parsed = JSON.parse(fs.readFileSync(filePath, 'utf8'));
    return parsed && typeof parsed === 'object' && !Array.isArray(parsed) ? parsed : null;
  } catch {
    return null;
  }
}

function serversMap(root, kind) {
  if (kind === 'mcpServers') {
    root.mcpServers = root.mcpServers && typeof root.mcpServers === 'object' ? root.mcpServers : {};
    return root.mcpServers;
  }
  if (kind === 'vscodeServers') {
    root.servers = root.servers && typeof root.servers === 'object' ? root.servers : {};
    return root.servers;
  }
  if (kind === 'zcode') {
    root.mcp = root.mcp && typeof root.mcp === 'object' ? root.mcp : {};
    root.mcp.servers = root.mcp.servers && typeof root.mcp.servers === 'object' ? root.mcp.servers : {};
    return root.mcp.servers;
  }
  root.mcp = root.mcp && typeof root.mcp === 'object' ? root.mcp : {};
  return root.mcp;
}

function managedServerIds() {
  return Object.values(SERVER_IDS);
}

function writeJsonIfChanged(filePath, root, { createdSkeleton, dryRun }) {
  const content = `${JSON.stringify(root, null, 2)}\n`;
  const before = fs.existsSync(filePath) ? fs.readFileSync(filePath, 'utf8') : null;
  if (before === content) {
    return 'unchanged';
  }
  if (!dryRun) {
    fs.mkdirSync(path.dirname(filePath), { recursive: true });
    fs.writeFileSync(filePath, content, 'utf8');
  }
  return before === null && createdSkeleton ? 'created' : 'updated';
}

// -- public API -----------------------------------------------------------------

/** Which managed servers does each agent config currently reference? */
export function agentStates(repoRoot) {
  const states = {};
  for (const agent of AGENT_LIST) {
    const filePath = path.join(repoRoot, agent.file);
    states[agent.id] = { configured: [], present: fs.existsSync(filePath) };
    if (agent.kind === 'codexToml') {
      if (states[agent.id].present) {
        const text = fs.readFileSync(filePath, 'utf8');
        states[agent.id].configured = managedServerIds().filter((id) => text.includes(`[mcp_servers.${id}]`));
      }
      continue;
    }
    const root = states[agent.id].present ? readJsonObject(filePath) : null;
    if (root) {
      const map = serversMap(root, agent.kind);
      states[agent.id].configured = managedServerIds().filter((id) => Object.prototype.hasOwnProperty.call(map, id));
    }
  }
  return states;
}

/**
 * Write (or replace) our backend entry in one agent's config file. Switching
 * backends removes both managed entries first. Never touches other entries.
 *
 * @returns {{agent: object, file: string, action: 'created'|'updated'|'unchanged'}}
 */
export function configureAgent({ repoRoot, agentId, backend, dryRun = false }) {
  const agent = agentById(agentId);
  if (!agent) {
    throw new Error(`unknown agent ${agentId}`);
  }
  const filePath = path.join(repoRoot, agent.file);
  let action = 'unchanged';

  if (agent.kind === 'codexToml') {
    let text = fs.existsSync(filePath) ? fs.readFileSync(filePath, 'utf8') : '';
    const previous = text;
    text = removeCodexTomlServers(text).text;
    if (!text.endsWith('\n') && text !== '') {
      text += '\n';
    }
    text += `${codexTomlSection(repoRoot, backend)}\n`;
    if (previous !== text) {
      action = previous === '' ? 'created' : 'updated';
      if (!dryRun) {
        fs.mkdirSync(path.dirname(filePath), { recursive: true });
        fs.writeFileSync(filePath, text, 'utf8');
      }
    }
    return { agent, file: filePath, action };
  }

  const existed = fs.existsSync(filePath);
  const root = (existed ? readJsonObject(filePath) : null) ?? {};
  const map = serversMap(root, agent.kind);
  for (const id of managedServerIds()) {
    delete map[id];
  }
  const entry =
    agent.kind === 'vscodeServers'
      ? vscodeServerEntry(repoRoot, backend)
      : agent.kind === 'zcode'
        ? zcodeServerEntry(repoRoot, backend)
        : agent.kind === 'opencode'
          ? opencodeServerEntry(repoRoot, backend)
          : mcpServersEntry(repoRoot, backend);
  map[BACKENDS[backend].serverId] = entry;
  action = writeJsonIfChanged(filePath, root, { createdSkeleton: !existed, dryRun });
  return { agent, file: filePath, action };
}

/**
 * Remove every managed server entry from one agent's config file. Files that
 * end up containing nothing but an empty skeleton (i.e. we created them) are
 * deleted.
 *
 * @returns {{agent: object, file: string, action: 'updated'|'removed-file'|'unchanged'}}
 */
export function removeAgentServers({ repoRoot, agentId, dryRun = false }) {
  const agent = agentById(agentId);
  if (!agent) {
    throw new Error(`unknown agent ${agentId}`);
  }
  const filePath = path.join(repoRoot, agent.file);
  if (!fs.existsSync(filePath)) {
    return { agent, file: filePath, action: 'unchanged' };
  }

  if (agent.kind === 'codexToml') {
    const previous = fs.readFileSync(filePath, 'utf8');
    const { text, changed } = removeCodexTomlServers(previous);
    if (!changed) {
      return { agent, file: filePath, action: 'unchanged' };
    }
    if (text.trim() === '') {
      if (!dryRun) {
        fs.rmSync(filePath);
      }
      return { agent, file: filePath, action: 'removed-file' };
    }
    if (!dryRun) {
      fs.writeFileSync(filePath, text, 'utf8');
    }
    return { agent, file: filePath, action: 'updated' };
  }

  const root = readJsonObject(filePath);
  if (!root) {
    return { agent, file: filePath, action: 'unchanged' };
  }
  const map = serversMap(root, agent.kind);
  let touched = false;
  for (const id of managedServerIds()) {
    if (Object.prototype.hasOwnProperty.call(map, id)) {
      delete map[id];
      touched = true;
    }
  }
  if (!touched) {
    return { agent, file: filePath, action: 'unchanged' };
  }
  const rootKey = JSON_ROOT_KEYS[agent.kind];
  const onlyOursLeft =
    Object.keys(root).every((key) => key === rootKey || (agent.kind === 'zcode' && key === 'mcp')) &&
    Object.keys(map).length === 0 &&
    (agent.kind !== 'zcode' || Object.keys(root.mcp).every((key) => key === 'servers'));
  if (onlyOursLeft) {
    if (!dryRun) {
      fs.rmSync(filePath);
    }
    return { agent, file: filePath, action: 'removed-file' };
  }
  if (!dryRun) {
    fs.writeFileSync(filePath, `${JSON.stringify(root, null, 2)}\n`, 'utf8');
  }
  return { agent, file: filePath, action: 'updated' };
}

/** Both managed server ids, used when switching backends. */
export function allManagedServerIds() {
  return managedServerIds();
}
