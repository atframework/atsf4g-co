/**
 * Product registry for the agent auto-configuration component.
 *
 * A *product* is what the user selects (an agent CLI/IDE, possibly sharing a
 * config file with other products); a *target* describes one physical config
 * file inside the repository. Keeping them separate lets several products
 * point at the same file (`.mcp.json` is shared by Claude Code / pi /
 * CodeBuddy CLI) without confusing selection with file layout.
 *
 * Targets may declare `candidates` (priority order; where more than one may
 * exist the mode decides: `exclusive` treats multiple existing candidates as
 * a conflict, `priority` uses the first existing one) and `legacyFiles`
 * (older locations whose managed entries are cleaned after the primary target
 * is written — migration rules in Plan.md 11.3).
 *
 * `evidence` is the official documentation page the file location and shape
 * were verified against (research rounds 2026-09-20; re-verified live before
 * P9.3 implementation — see Plan.md 11.11). Guidance-only clients (Cline,
 * CodeBuddy IDE) are intentionally absent: they get explicit guidance text in
 * P9.4, never fabricated project files.
 */

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

/** All managed server ids; switching backends removes both before writing one. */
export function managedServerIds() {
  return Object.values(SERVER_IDS);
}

export const TARGETS = Object.freeze({
  // Shared by Claude Code / pi / CodeBuddy CLI. Candidates follow CodeBuddy's
  // official priority (`.mcp.json` first, deprecated root `mcp.json` second,
  // only the first existing file is read); the in-place legacy rule for
  // sole-codebuddy selections lives in configPlan (Plan.md 11.3.4).
  'claude-mcp-json': {
    file: '.mcp.json',
    candidates: ['.mcp.json', 'mcp.json'],
    candidatesMode: 'priority',
    legacyFallback: { candidate: 'mcp.json', inPlaceOwnerIds: ['codebuddy'] },
    format: 'mcpServers',
    pathPolicy: 'absolute-args',
    evidence: 'https://code.claude.com/docs/en/mcp + https://www.codebuddy.cn/docs/cli/mcp',
  },
  'codex-config-toml': {
    file: path.join('.codex', 'config.toml'),
    format: 'codexToml',
    pathPolicy: 'cwd-relative',
    evidence: 'https://learn.chatgpt.com/docs/extend/mcp?surface=cli',
  },
  'vscode-mcp-json': {
    file: path.join('.vscode', 'mcp.json'),
    format: 'vscodeServers',
    pathPolicy: 'workspace-folder',
    evidence: 'https://code.visualstudio.com/docs/agent-customization/mcp-servers',
  },
  'cursor-mcp-json': {
    file: path.join('.cursor', 'mcp.json'),
    format: 'mcpServers',
    pathPolicy: 'absolute-args',
    evidence: 'https://cursor.com/docs/mcp',
  },
  'gemini-settings-json': {
    file: path.join('.gemini', 'settings.json'),
    format: 'mcpServers',
    pathPolicy: 'absolute-args',
    evidence: 'https://geminicli.com/docs/tools/mcp-server/',
  },
  'opencode-json': {
    file: 'opencode.json',
    candidates: ['opencode.json', 'opencode.jsonc'],
    candidatesMode: 'exclusive',
    format: 'opencode',
    pathPolicy: 'absolute-args',
    evidence: 'https://opencode.ai/docs/mcp-servers/ + https://opencode.ai/docs/config/',
  },
  // Current Kilo shape (OpenCode-like). Kilo loads `./kilo.json(c)` AND
  // `.kilo/kilo.json(c)` as separate precedence levels and deep-merges them —
  // they are not either/or variants, so coexistence is normal and must not
  // conflict. Only same-directory json/jsonc variants are ambiguous
  // (exclusive). Machine-specific entries always go to the gitignored
  // `.kilo/` location; managed entries found in root files are migrated
  // (legacy-style cleanup), foreign entries untouched.
  'kilo-json': {
    file: path.join('.kilo', 'kilo.json'),
    candidates: [path.join('.kilo', 'kilo.json'), path.join('.kilo', 'kilo.jsonc')],
    candidatesMode: 'exclusive',
    legacyFiles: [
      { file: 'kilo.json', format: 'opencode' },
      { file: 'kilo.jsonc', format: 'opencode' },
      path.join('.kilocode', 'mcp.json'),
    ],
    format: 'opencode',
    pathPolicy: 'absolute-args',
    evidence: 'https://kilo.ai/docs/automate/mcp/using-in-kilo-code + https://kilo.ai/docs/getting-started/settings',
  },
  'roo-mcp-json': {
    file: path.join('.roo', 'mcp.json'),
    legacyFiles: [path.join('.roo', 'mcp_settings.json')],
    format: 'mcpServers',
    pathPolicy: 'absolute-args',
    evidence: 'https://roocodeinc.github.io/Roo-Code/features/mcp/using-mcp-in-roo/',
  },
  'zcode-config-json': {
    file: path.join('.zcode', 'config.json'),
    format: 'zcode',
    pathPolicy: 'absolute-args',
    evidence: 'https://www.zcode.network/en/docs/mcp-services/',
  },
  'omp-mcp-json': {
    file: path.join('.omp', 'mcp.json'),
    format: 'mcpServers',
    pathPolicy: 'absolute-args',
    evidence: 'https://github.com/can1357/oh-my-pi/blob/main/docs/mcp-config.md',
  },
  // Zed project settings are JSONC and hold unrelated settings; edits are
  // surgical so foreign content survives.
  'zed-settings-json': {
    file: path.join('.zed', 'settings.json'),
    format: 'zed',
    pathPolicy: 'absolute-args',
    evidence: 'https://zed.dev/docs/ai/mcp + https://zed.dev/docs/migrate/vs-code',
  },
  'kimi-code-mcp-json': {
    file: path.join('.kimi-code', 'mcp.json'),
    format: 'mcpServersCwd',
    pathPolicy: 'cwd-relative',
    evidence: 'https://github.com/MoonshotAI/kimi-code/blob/main/docs/en/customization/mcp.md',
  },
  'qwen-settings-json': {
    file: path.join('.qwen', 'settings.json'),
    format: 'mcpServersCwd',
    pathPolicy: 'cwd-relative',
    evidence: 'https://qwenlm.github.io/qwen-code-docs/en/users/features/mcp/',
  },
  'workbuddy-mcp-json': {
    file: path.join('.workbuddy', 'mcp.json'),
    format: 'mcpServers',
    pathPolicy: 'absolute-args',
    evidence: 'https://www.codebuddy.cn/docs/workbuddy/From-Beginner-to-Expert-Guide/Function-Description/MCP-Guide',
  },
  'mimocode-json': {
    file: path.join('.mimocode', 'mimocode.json'),
    candidates: ['mimocode.json', 'mimocode.jsonc', path.join('.mimocode', 'mimocode.json'), path.join('.mimocode', 'mimocode.jsonc')],
    candidatesMode: 'exclusive',
    format: 'opencode',
    pathPolicy: 'absolute-args',
    evidence: 'https://mimo.xiaomi.com/mimocode/mcp-servers + https://github.com/XiaomiMiMo/MiMo-Code/blob/main/packages/opencode/src/config/config.ts',
  },
});

export const REGISTRY = Object.freeze([
  { id: 'claude', label: 'Claude Code (与 pi、CodeBuddy CLI 共用 .mcp.json)', aliases: [], capability: 'auto', targetId: 'claude-mcp-json' },
  { id: 'pi', label: 'pi (需 pi-mcp-adapter 扩展，共用 .mcp.json)', aliases: [], capability: 'extension', prerequisites: 'pi 本体不内置 MCP：需安装 nicobailon/pi-mcp-adapter 后才会读取项目 .mcp.json', targetId: 'claude-mcp-json' },
  { id: 'codebuddy', label: 'CodeBuddy Code CLI (共用 .mcp.json)', aliases: [], capability: 'auto', targetId: 'claude-mcp-json' },
  { id: 'codex', label: 'Codex CLI', aliases: [], capability: 'auto', targetId: 'codex-config-toml' },
  { id: 'vscode', label: 'VS Code / GitHub Copilot', aliases: [], capability: 'auto', targetId: 'vscode-mcp-json' },
  { id: 'cursor', label: 'Cursor', aliases: [], capability: 'auto', targetId: 'cursor-mcp-json' },
  { id: 'gemini', label: 'Gemini CLI', aliases: [], capability: 'auto', targetId: 'gemini-settings-json' },
  { id: 'opencode', label: 'OpenCode', aliases: [], capability: 'auto', targetId: 'opencode-json' },
  { id: 'kilo', label: 'Kilo Code (从 .kilocode 迁移)', aliases: ['kilocode'], capability: 'auto', targetId: 'kilo-json' },
  { id: 'roo', label: 'Roo Code (从 .roo/mcp_settings.json 迁移)', aliases: [], capability: 'auto', targetId: 'roo-mcp-json' },
  { id: 'zcode', label: 'ZCode', aliases: [], capability: 'auto', targetId: 'zcode-config-json' },
  { id: 'omp', label: 'oh-my-pi (omp)', aliases: [], capability: 'auto', targetId: 'omp-mcp-json' },
  { id: 'zed', label: 'Zed', aliases: [], capability: 'auto', targetId: 'zed-settings-json' },
  { id: 'kimi-code', label: 'Kimi Code CLI', aliases: [], capability: 'auto', targetId: 'kimi-code-mcp-json' },
  { id: 'qwen', label: 'Qwen Code', aliases: [], capability: 'auto', targetId: 'qwen-settings-json' },
  { id: 'workbuddy', label: 'WorkBuddy', aliases: [], capability: 'auto', targetId: 'workbuddy-mcp-json' },
  { id: 'mimocode', label: 'MiMo Code', aliases: [], capability: 'auto', targetId: 'mimocode-json' },
]);

export function agentDefinitions() {
  return REGISTRY;
}

export function targetFor(agent) {
  return TARGETS[agent.targetId];
}

/** Normalize old string locations and locations that declare a different format. */
export function legacyLocations(target) {
  return (target.legacyFiles ?? []).map((entry) => typeof entry === 'string' ? { file: entry, format: 'mcpServers' } : entry);
}

export function agentById(agentId) {
  return REGISTRY.find((agent) => agent.id === agentId || agent.aliases.includes(agentId)) ?? null;
}

/** Products whose target resolves to the given targetId (shared-file group). */
export function productsForTarget(targetId) {
  return REGISTRY.filter((agent) => agent.targetId === targetId);
}
