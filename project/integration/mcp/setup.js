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
 * CodeGraph 同批更新根 AGENTS.md 的条件式提示词；等价正文不重复写入。
 *
 * 可重复执行：用于切换后端、升级固定制品、为更多 Agent 接入或卸载。
 * 常用选项：
 *   --backend=tgrep|codegraph   指定后端（跳过交互）
 *   --agents=<id1,id2,...>|all  指定要接入的 Agent（跳过交互，--list-agents 查看可选 id）
 *   --mirror=cn|official        指定下载镜像（缺省按地区建议并交互确认）
 *   --offline                   只使用已缓存的制品
 *   --skip-prepare              跳过依赖准备，仅调整 Agent 配置
 *   --uninstall                 移除 Agent 配置中的本集成条目（配合 --agents/--all-agents）
 *   --all-agents                卸载时清理全部 Agent
 *   --yes                       非交互执行（需配合上述显式选项）
 *   --dry-run                   只打印动作，不写任何文件、不准备依赖、不联网
 *   --help / --list-agents      打印用法 / Agent 列表后退出（无副作用）
 */

import fs from 'node:fs';
import readline from 'node:readline/promises';
import path from 'node:path';

import { AgentConfigError, BACKENDS, agentDefinitions, agentStates, buildAgentOperations, productsForTarget, runAgentConfigBatch } from './agents/src/writers.mjs';
import { targetFor, agentById } from './agents/src/registry.mjs';
import { runPrepare, writePreparedState } from './common/src/prepare.mjs';
import { WorkspacePaths, deriveRepoRoot, resolveBuildDir } from './common/src/paths.mjs';

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
  const options = { help: false, listAgents: false, uninstall: false, allAgents: false, yes: false, dryRun: false, offline: false, skipPrepare: false, backend: null, agents: null, mirror: null };
  const requireValue = (flag, value) => {
    if (!value.trim()) {
      throw new Error(`${flag} 需要非空值（--help 查看用法）`);
    }
    return value.trim();
  };
  for (const arg of argv.slice(2)) {
    if (arg === '--help' || arg === '-h') options.help = true;
    else if (arg === '--list-agents') options.listAgents = true;
    else if (arg === '--uninstall') options.uninstall = true;
    else if (arg === '--all-agents') options.allAgents = true;
    else if (arg === '--yes' || arg === '-y') options.yes = true;
    else if (arg === '--dry-run') options.dryRun = true;
    else if (arg === '--offline') options.offline = true;
    else if (arg === '--skip-prepare') options.skipPrepare = true;
    else if (arg.startsWith('--backend=')) options.backend = requireValue('--backend', arg.slice('--backend='.length));
    else if (arg.startsWith('--agents=')) options.agents = requireValue('--agents', arg.slice('--agents='.length));
    else if (arg.startsWith('--mirror=')) options.mirror = requireValue('--mirror', arg.slice('--mirror='.length));
    else {
      throw new Error(`未知参数：${arg}（--help 查看用法）`);
    }
  }
  if (options.backend && !Object.hasOwn(BACKENDS, options.backend)) {
    throw new Error(`--backend 仅支持：${Object.keys(BACKENDS).join(' | ')}`);
  }
  if (options.mirror && !Object.hasOwn(NPM_MIRRORS, options.mirror)) {
    throw new Error(`--mirror 仅支持：${Object.keys(NPM_MIRRORS).join(' | ')}`);
  }
  return options;
}

function printHelp() {
  const usage = [
    '用法：node project/integration/mcp/setup.js [选项]',
    '',
    '流程：选择检索后端（tgrep / CodeGraph 二选一）→ 选择下载镜像 → 准备本地依赖',
    '（全部固定版本）→ 依赖就绪后把 MCP 服务写入所选 Agent 的项目级配置。',
    '依赖准备失败时不会改动任何 Agent 配置。可重复执行，用于切换后端、升级固定',
    '制品、为更多 Agent 接入或卸载。',
    'CodeGraph 接入会同批更新根 AGENTS.md 的提示词；编码/换行差异不触发重复写入。',
    '',
    '选项：',
    '  --backend=tgrep|codegraph   指定后端（跳过交互）',
    '  --agents=<id1,id2,...>|all  指定要接入的 Agent（跳过交互；--list-agents 查看可选 id）',
    '  --mirror=cn|official        指定下载镜像（缺省按地区建议并交互确认）',
    '  --offline                   只使用已缓存的制品',
    '  --skip-prepare              跳过依赖准备，仅调整 Agent 配置',
    '  --uninstall                 移除 Agent 配置中的本集成条目（配合 --agents/--all-agents）',
    '  --all-agents                卸载时清理全部 Agent',
    '  --yes                       非交互执行（需配合上述显式选项）',
    '  --dry-run                   只打印动作，不写任何文件、不准备依赖、不联网',
    '  --help, -h                  显示本帮助（无副作用）',
    '  --list-agents               列出可配置的 Agent 及其配置文件（无副作用）',
    '',
    '安装语义：所选目标构成最终集合——已配置但未选中的托管目标会被移除。',
    '卸载用 --uninstall [--agents=...|--all-agents]；两者都不影响索引与已下载制品。',
  ];
  process.stdout.write(`${usage.join('\n')}\n`);
}

function printAgentList() {
  process.stdout.write('可配置的 Agent（id — 说明 — 仓库内配置文件）：\n');
  for (const agent of agentDefinitions()) {
    process.stdout.write(`  ${agent.id.padEnd(10)} ${agent.label}  (${targetFor(agent).file.split(path.sep).join('/')})\n`);
  }
  process.stdout.write('用 --agents=<id,...> 选择；docs-only 客户端（Windsurf/Cline）见 README 手工配置示例。\n');
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
    const state = states[agent.id] ?? {};
    const note = state.error ? '配置文件异常（相关写入会中止）' : (state.configured ?? []).length > 0 ? `已配置 ${state.configured.join(', ')}` : '未配置';
    return { key: agent.id, label: agent.label, note };
  });
}

/** Resolve --agents=... to concrete ids; rejects unknown ids before any prompt or download. */
function resolveAgentIds(value) {
  if (!value || value === 'all') {
    return agentDefinitions().map((agent) => agent.id);
  }
  const ids = value.split(/[,，\s]+/).filter(Boolean);
  if (ids.length === 0) throw new Error('--agents 需要至少一个 Agent id');
  const unknown = ids.filter((id) => !agentById(id));
  if (unknown.length > 0) {
    throw new Error(`未知 Agent：${unknown.join(', ')}（可用 id 用 --list-agents 查看）`);
  }
  return [...new Set(ids.map((id) => agentById(id).id))];
}

/** Backend recorded by the last successful prepare (read-only; agent configs are not scanned before dependencies exist). */
function preparedBackend(paths) {
  try {
    const parsed = JSON.parse(fs.readFileSync(paths.preparedStatePath(), 'utf8'));
    return parsed && typeof parsed === 'object' && Object.hasOwn(BACKENDS, parsed.backend) ? parsed.backend : null;
  } catch {
    return null;
  }
}

/**
 * Plan every config change first (read-only), then apply. A target that cannot
 * be parsed safely aborts the whole batch before any file is written; returns
 * false after printing the diagnostics.
 */
function runConfigBatch(ui, paths, operations, { dryRun }) {
  const { plan } = runAgentConfigBatch({
    repoRoot: paths.repoRoot,
    operations,
    dryRun,
    stateDir: paths.stateDir,
    tmpDir: paths.agentTmpDir,
    codegraphGuidance: operations.some((op) => op.type === 'configure' && op.backend === 'codegraph'),
  });
  if (plan.problems.length > 0) {
    ui.line('');
    for (const problem of plan.problems) {
      ui.line(`配置目标 ${problem.targetId}（${problem.agents.join(', ')}）无法安全处理，本轮全部写入已中止，文件保持原样：`);
      ui.line(`  ${problem.error.message}`);
    }
    return false;
  }
  for (const note of plan.notes) {
    ui.line(`提示：${note}`);
  }
  const prefix = dryRun ? '拟' : '已';
  const verbs = { create: '新建', update: '更新', delete: '删除', unchanged: '无变化' };
  for (const step of plan.steps) {
    if (step.guidance) {
      ui.line(`${step.action === 'unchanged' ? '无变化' : prefix + verbs[step.action]}：${step.relative} 中的 CodeGraph 提示词`);
      continue;
    }
    if (step.legacy) {
      ui.line(`${step.action === 'unchanged' ? '无变化' : `${prefix}清理`}旧格式条目：${step.relative}`);
      continue;
    }
    const consumers = productsForTarget(step.target.targetId).map((product) => product.id);
    const label = consumers.length > 1 ? `${step.relative}（${consumers.join(' / ')} 共用，移除会影响全组）` : step.relative;
    if (step.ops.some((op) => op.reason === 'unselected')) {
      ui.line(`${step.action === 'unchanged' ? '无变化' : `${prefix}移除（未选中）`}：${label}`);
      continue;
    }
    ui.line(`${step.action === 'unchanged' ? '无变化' : prefix + verbs[step.action]}：${label}`);
  }
  if (plan.steps.length === 0) {
    ui.line('没有需要变更的 Agent 配置。');
  }
  return true;
}

async function runSetup(options, ui) {
  const repoRoot = deriveRepoRoot(new URL('./setup.js', import.meta.url), null);
  const buildDir = resolveBuildDir(repoRoot, null);
  const paths = new WorkspacePaths(repoRoot, buildDir);

  ui.line('atsf4g-co MCP 集成安装');
  ui.line(`仓库：${repoRoot}`);
  ui.line(`构建目录：${buildDir}`);
  ui.line('');

  // -- 卸载模式（不准备依赖，只读扫描配置） ----------------------------------------
  if (options.uninstall) {
    const states = agentStates(repoRoot);
    let targets;
    if (options.agents || options.allAgents) {
      targets = options.allAgents && !options.agents ? agentDefinitions().map((agent) => agent.id) : resolveAgentIds(options.agents);
    } else if (options.yes) {
      throw new Error('--uninstall --yes 需要 --agents=... 或 --all-agents');
    } else {
      const damaged = Object.entries(states).filter(([, state]) => state.error);
      if (damaged.length) throw new Error(damaged.map(([id, state]) => `${id}: ${state.error}`).join('\n'));
      const current = agentDefinitions().filter((agent) => (states[agent.id]?.configured ?? []).length > 0).map((agent) => agent.id);
      if (current.length === 0) {
        ui.line('当前没有 Agent 配置本集成，无需卸载。');
        return 0;
      }
      const selected = await ui.checkbox('选择要从哪些 Agent 移除本集成：', agentEntries(states).filter((entry) => (states[entry.key]?.configured ?? []).length > 0), current);
      targets = [...selected];
    }
    if (!runConfigBatch(ui, paths, targets.map((agentId) => ({ type: 'remove', agentId })), { dryRun: options.dryRun })) {
      return 1;
    }
    ui.line('');
    ui.line('卸载只移除 Agent 配置；索引与已下载制品保留在构建目录和 .codegraph* 下，可按需手动删除。');
    return 0;
  }

  // -- 选择后端（同类二选一；建议值来自上次准备记录，不提前读 Agent 配置） ------------
  let backend = options.backend;
  if (!backend) {
    const prepared = preparedBackend(paths);
    if (options.yes) {
      if (!prepared) {
        throw new Error('--yes 模式下缺少 --backend，且没有可参考的已准备依赖记录（先执行一次交互安装或显式指定 --backend）');
      }
      backend = prepared;
    } else {
      const choices = Object.entries(BACKENDS).map(([id, def]) => {
        const mark = id === prepared ? '（上次准备）' : '';
        return `${def.label}${mark}`;
      });
      const index = await ui.select('选择检索后端（同类工具二选一）：', choices, { defaultIndex: prepared ? Object.keys(BACKENDS).indexOf(prepared) : 0 });
      backend = Object.keys(BACKENDS)[index];
    }
  }

  // -- 准备依赖（失败则绝不触碰 Agent 配置；dry-run 完全跳过准备） -------------------
  if (options.dryRun) {
    const mirror = options.mirror ?? suggestMirror();
    ui.line(`dry-run：拟准备依赖（后端：${backend}${mirror ? `，镜像：${mirror}` : ''}），不执行下载、安装或状态写入。`);
  } else if (!options.skipPrepare) {
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

  // -- 选择 Agent（可多选；已配置的默认保持）——依赖就绪后才扫描配置 ------------------
  const states = agentStates(repoRoot);
  let selectedIds;
  if (options.agents) {
    selectedIds = new Set(resolveAgentIds(options.agents));
  } else if (options.yes) {
    const damaged = Object.entries(states).filter(([, state]) => state.error);
    if (damaged.length) throw new Error(damaged.map(([id, state]) => `${id}: ${state.error}`).join('\n'));
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

  // -- 两阶段写入：先全量规划（含损坏/候选/迁移检测），全部通过后再统一 apply ----------
  const operations = buildAgentOperations({ states, selectedIds, backend });
  if (!runConfigBatch(ui, paths, operations, { dryRun: options.dryRun })) {
    return 1;
  }
  if (selectedIds.has('pi')) {
    ui.line(`提示：pi 本体不内置 MCP，需安装 nicobailon/pi-mcp-adapter 扩展后才会读取项目 .mcp.json；${options.dryRun ? '本轮仅预览' : '配置已生成'}，扩展加载与连接待验证。`);
  }

  ui.line('');
  if (options.dryRun) {
    ui.line('dry-run 结束：以上动作均未执行（未准备依赖、未写配置）。');
  } else {
    ui.line(`完成。后端：${backend}；重启对应的 Agent 客户端后生效。`);
  }
  ui.line('诊断：node project/integration/mcp/common/tools/doctor.mjs');
  return 0;
}

async function main() {
  const options = parseArgv(process.argv);
  if (options.help) {
    printHelp();
    return 0;
  }
  if (options.listAgents) {
    printAgentList();
    return 0;
  }
  // Unknown agent ids must fail before any prompt, download, or config scan.
  resolveAgentIds(options.agents);
  const io = options.yes || !process.stdin.isTTY || !process.stdout.isTTY
    ? {
        question: async () => {
          throw new Error('非交互执行缺少必要选项，请用 --yes 并显式指定 --backend、--agents、--mirror，或在终端中运行');
        },
        close() {},
      }
    : readline.createInterface({ input: process.stdin, output: process.stdout });
  const ui = new Ui(io);
  try {
    return await runSetup(options, ui);
  } finally {
    io.close();
  }
}

try {
  process.exitCode = await main();
} catch (error) {
  process.stderr.write(`setup: ${error.message}\n`);
  if (error instanceof AgentConfigError) {
    process.stderr.write('该配置文件已保持原样（未做任何写入）。请先修复或整理该文件后重试。\n');
  }
  process.exitCode = 1;
}
