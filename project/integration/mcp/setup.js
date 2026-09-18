#!/usr/bin/env node
/**
 * atsf4g-co MCP 集成安装入口。
 *
 * 用法：
 *   node project/integration/mcp/setup.js                     交互式安装 / 切换
 *   node project/integration/mcp/setup.js --uninstall         从 Agent 配置移除本集成
 *
 * 流程：选择检索后端（tgrep / CodeGraph 二选一）→ 选择下载镜像 → 准备本地依赖
 * （npm + cargo / npm pack，全部固定版本）→ 全部成功后才写入所选 Agent 的
 * 项目级 MCP 配置。依赖准备失败时不会改动任何 Agent 配置。
 *
 * 可重复执行：用于切换后端、升级固定制品、为更多 Agent 接入或卸载。
 * 常用选项：
 *   --backend=tgrep|codegraph   指定后端（跳过交互）
 *   --agents=<id1,id2,...>|all  指定要接入的 Agent（跳过交互）
 *   --mirror=cn|official        指定下载镜像（缺省按地区建议并交互确认）
 *   --offline                   只使用已缓存的制品
 *   --skip-prepare              跳过依赖准备，仅调整 Agent 配置
 *   --uninstall                 移除 Agent 配置中的本集成条目（配合 --agents/--all-agents）
 *   --all-agents                卸载时清理全部 Agent
 *   --yes                       非交互执行（需配合上述显式选项）
 *   --dry-run                   只打印动作，不写文件
 */

import readline from 'node:readline/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

import { BACKENDS, agentDefinitions, agentStates, configureAgent, removeAgentServers } from './common/src/agents.mjs';
import { runPrepare, writePreparedState } from './common/src/prepare.mjs';
import { WorkspacePaths, deriveRepoRoot, resolveBuildDir } from './common/src/paths.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));

const NPM_MIRRORS = {
  cn: 'https://registry.npmmirror.com',
  official: null,
};
const CARGO_MIRROR_ARGS = [
  '--config',
  'source.crates-io.replace-with="rsproxy"',
  '--config',
  'source.rsproxy.registry="sparse+https://rsproxy.cn/index/"',
];

function parseArgv(argv) {
  const options = { uninstall: false, allAgents: false, yes: false, dryRun: false, offline: false, skipPrepare: false, backend: null, agents: null, mirror: null };
  for (const arg of argv.slice(2)) {
    if (arg === '--uninstall') options.uninstall = true;
    else if (arg === '--all-agents') options.allAgents = true;
    else if (arg === '--yes' || arg === '-y') options.yes = true;
    else if (arg === '--dry-run') options.dryRun = true;
    else if (arg === '--offline') options.offline = true;
    else if (arg === '--skip-prepare') options.skipPrepare = true;
    else if (arg.startsWith('--backend=')) options.backend = arg.slice('--backend='.length);
    else if (arg.startsWith('--agents=')) options.agents = arg.slice('--agents='.length);
    else if (arg.startsWith('--mirror=')) options.mirror = arg.slice('--mirror='.length);
    else {
      throw new Error(`未知参数：${arg}（--help 查看用法）`);
    }
  }
  if (options.backend && !BACKENDS[options.backend]) {
    throw new Error(`--backend 仅支持：${Object.keys(BACKENDS).join(' | ')}`);
  }
  if (options.mirror && !(options.mirror in NPM_MIRRORS)) {
    throw new Error(`--mirror 仅支持：${Object.keys(NPM_MIRRORS).join(' | ')}`);
  }
  return options;
}

/** 国内环境建议使用国内镜像：按时区/语言给出建议值，最终由用户确认。 */
function suggestMirror() {
  const timezone = Intl.DateTimeFormat().resolvedOptions().timeZone ?? '';
  const cnTimezones = ['Asia/Shanghai', 'Asia/Urumqi', 'Asia/Chongqing', 'Asia/Harbin', 'Asia/Hong_Kong', 'Asia/Macau'];
  const locale = `${process.env.LANG ?? ''}${process.env.LC_ALL ?? ''}`;
  if (cnTimezones.includes(timezone) || /^zh/i.test(locale)) {
    return 'cn';
  }
  return 'official';
}

class Ui {
  /** `io` only needs `question()` (readline/promises interface); all output
   *  goes through process.stdout directly -- readline's write() is unreliable
   *  when stdin is not a TTY. */
  constructor(io) {
    this.io = io;
  }
  line(text = '') {
    process.stdout.write(`${text}\n`);
  }
  async ask(prompt) {
    return (await this.io.question(prompt)).trim();
  }
  async select(prompt, choices, { defaultIndex = 0 } = {}) {
    this.line(prompt);
    choices.forEach((choice, index) => {
      this.line(`  ${index + 1}. ${choice}${index === defaultIndex ? '（回车默认）' : ''}`);
    });
    while (true) {
      const answer = (await this.ask('>')) || String(defaultIndex + 1);
      const index = Number(answer) - 1;
      if (Number.isInteger(index) && index >= 0 && index < choices.length) {
        return index;
      }
      this.line(`请输入 1-${choices.length} 的编号。`);
    }
  }
  async checkbox(prompt, entries, defaults) {
    // entries: [{key, label, note}]; 返回选中的 key 集合。
    this.line(prompt);
    entries.forEach((entry, index) => {
      const mark = defaults.includes(entry.key) ? '[x]' : '[ ]';
      this.line(`  ${index + 1}. ${mark} ${entry.label}${entry.note ? ` — ${entry.note}` : ''}`);
    });
    this.line('输入编号列表（如 1,3,5）、a=全选、n=全不选，回车保持上面的勾选状态。');
    while (true) {
      const answer = (await this.ask('>')).toLowerCase();
      if (answer === '') {
        return new Set(defaults);
      }
      if (answer === 'a') {
        return new Set(entries.map((entry) => entry.key));
      }
      if (answer === 'n') {
        return new Set();
      }
      const picks = answer.split(/[,，\s]+/).map((value) => Number(value) - 1);
      if (picks.every((index) => Number.isInteger(index) && index >= 0 && index < entries.length)) {
        return new Set(picks.map((index) => entries[index].key));
      }
      this.line('无法识别输入，请重试。');
    }
  }
}

function agentEntries(states) {
  return agentDefinitions().map((agent) => {
    const configured = states[agent.id]?.configured ?? [];
    const note = configured.length > 0 ? `已配置 ${configured.join(', ')}` : '未配置';
    return { key: agent.id, label: agent.label, note };
  });
}

function agentIdsFromOption(value, states) {
  if (!value || value === 'all') {
    return agentDefinitions().map((agent) => agent.id);
  }
  const known = new Set(agentDefinitions().map((agent) => agent.id));
  const ids = value.split(/[,，\s]+/).filter(Boolean);
  const unknown = ids.filter((id) => !known.has(id));
  if (unknown.length > 0) {
    throw new Error(`未知 Agent：${unknown.join(', ')}（可用：${[...known].join(', ')}）`);
  }
  return ids;
}

function configuredBackend(states) {
  const configured = new Set();
  for (const state of Object.values(states)) {
    for (const id of state.configured) {
      configured.add(id);
    }
  }
  for (const [backend, def] of Object.entries(BACKENDS)) {
    if (configured.has(def.serverId)) {
      return backend;
    }
  }
  return null;
}

async function main() {
  const options = parseArgv(process.argv);
  const io = options.yes
    ? { question: async () => { throw new Error('--yes 模式下缺少必要选项（后端/Agent 需显式指定）'); } }
    : readline.createInterface({ input: process.stdin, output: process.stdout });
  const ui = new Ui(io);

  const repoRoot = deriveRepoRoot(new URL('./setup.js', import.meta.url), null);
  const buildDir = resolveBuildDir(repoRoot, null);
  const paths = new WorkspacePaths(repoRoot, buildDir);
  const states = agentStates(repoRoot);

  ui.line('atsf4g-co MCP 集成安装');
  ui.line(`仓库：${repoRoot}`);
  ui.line(`构建目录：${buildDir}`);
  ui.line('');

  // -- 卸载模式 -----------------------------------------------------------------
  if (options.uninstall) {
    let targets;
    if (options.agents || options.allAgents) {
      targets = options.allAgents && !options.agents ? agentDefinitions().map((agent) => agent.id) : agentIdsFromOption(options.agents, states);
    } else if (options.yes) {
      throw new Error('--uninstall --yes 需要 --agents=... 或 --all-agents');
    } else {
      const current = agentDefinitions().filter((agent) => (states[agent.id]?.configured ?? []).length > 0).map((agent) => agent.id);
      if (current.length === 0) {
        ui.line('当前没有 Agent 配置本集成，无需卸载。');
        return 0;
      }
      const selected = await ui.checkbox('选择要从哪些 Agent 移除本集成：', agentEntries(states).filter((entry) => (states[entry.key]?.configured ?? []).length > 0), current);
      targets = [...selected];
    }
    for (const agentId of targets) {
      const result = removeAgentServers({ repoRoot, agentId, dryRun: options.dryRun });
      ui.line(`${result.action === 'removed-file' ? '已删除' : result.action === 'updated' ? '已更新' : '无变化（跳过）'}：${path.relative(repoRoot, result.file)}${options.dryRun ? '（dry-run）' : ''}`);
    }
    ui.line('');
    ui.line('卸载只移除 Agent 配置；索引与已下载制品保留在构建目录和 .codegraph* 下，可按需手动删除。');
    return 0;
  }

  // -- 选择后端（同类二选一） ------------------------------------------------------
  let backend = options.backend;
  if (!backend) {
    const current = configuredBackend(states);
    const choices = Object.entries(BACKENDS).map(([id, def]) => {
      const mark = id === current ? '（当前已配置）' : '';
      return `${def.label}${mark}`;
    });
    const index = await ui.select('选择检索后端（同类工具二选一）：', choices, { defaultIndex: current ? Object.keys(BACKENDS).indexOf(current) : 0 });
    backend = Object.keys(BACKENDS)[index];
  }

  // -- 准备依赖（失败则绝不触碰 Agent 配置） ---------------------------------------
  if (!options.skipPrepare) {
    let mirror = options.mirror;
    if (!mirror && !options.offline) {
      const suggested = suggestMirror();
      if (options.yes) {
        mirror = suggested;
      } else {
        const index = await ui.select('选择下载镜像（npm / cargo）：', ['cn — 国内镜像（npmmirror + rsproxy）', 'official — 官方源'], {
          defaultIndex: suggested === 'cn' ? 0 : 1,
        });
        mirror = index === 0 ? 'cn' : 'official';
      }
    }
    const npmRegistry = mirror && mirror in NPM_MIRRORS ? NPM_MIRRORS[mirror] : null;
    const cargoConfigArgs = mirror === 'cn' ? CARGO_MIRROR_ARGS : [];
    ui.line('');
    ui.line(`准备依赖（后端：${backend}${npmRegistry ? `，镜像：${mirror}` : ''}）…`);
    try {
      const result = runPrepare({
        repoRoot,
        buildDir,
        backend,
        offline: options.offline,
        npmRegistry,
        cargoConfigArgs,
        log: (message) => ui.line(message),
      });
      const statePath = writePreparedState(result.paths, result.state);
      ui.line(`状态已写入 ${statePath}`);
    } catch (error) {
      ui.line('');
      ui.line(`依赖准备失败，未修改任何 Agent 配置。`);
      ui.line(`${error.message}`);
      ui.line('排查建议：检查网络/镜像可达性后重试；或用 node project/integration/mcp/common/tools/doctor.mjs 查看当前状态。');
      return 1;
    }
  } else {
    ui.line('已按 --skip-prepare 跳过依赖准备。');
  }

  // -- 选择 Agent（可多选；已配置的默认保持） ---------------------------------------
  let selectedIds;
  if (options.agents) {
    selectedIds = new Set(agentIdsFromOption(options.agents, states));
  } else if (options.yes) {
    selectedIds = new Set(agentDefinitions().filter((agent) => (states[agent.id]?.configured ?? []).length > 0).map((agent) => agent.id));
    if (selectedIds.size === 0) {
      ui.line('--yes 且未指定 --agents：没有需要更新的已配置 Agent，结束。');
      return 0;
    }
  } else {
    const defaults = agentDefinitions().filter((agent) => (states[agent.id]?.configured ?? []).length > 0).map((agent) => agent.id);
    const hint = defaults.length === 0 ? '首次接入：勾选要配置的 Agent' : '回车保持当前已配置的 Agent';
    const selected = await ui.checkbox(`选择要接入的 Agent（${hint}）：`, agentEntries(states), defaults);
    selectedIds = selected;
  }

  ui.line('');
  const dryNote = options.dryRun ? '（dry-run）' : '';
  for (const agent of agentDefinitions()) {
    if (selectedIds.has(agent.id)) {
      const result = configureAgent({ repoRoot, agentId: agent.id, backend, dryRun: options.dryRun });
      const verb = result.action === 'created' ? '新建' : result.action === 'updated' ? '更新' : '无变化';
      ui.line(`${verb}：${path.relative(repoRoot, result.file)}${dryNote}`);
    } else if ((states[agent.id]?.configured ?? []).length > 0) {
      const result = removeAgentServers({ repoRoot, agentId: agent.id, dryRun: options.dryRun });
      ui.line(`移除（未选中）：${path.relative(repoRoot, result.file)}${dryNote}`);
    }
  }

  ui.line('');
  ui.line(`完成。后端：${backend}；重启对应的 Agent 客户端后生效。`);
  ui.line('诊断：node project/integration/mcp/common/tools/doctor.mjs');
  return 0;
}

try {
  process.exitCode = await main();
} catch (error) {
  process.stderr.write(`setup: ${error.message}\n`);
  process.exitCode = 1;
}
