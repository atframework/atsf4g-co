#!/usr/bin/env node
/**
 * workspace MCP 集成安装入口。
 *
 * 用法：
 *   node <MCP_DIR>/setup.js                     交互式安装 / 切换
 *   bun <MCP_DIR>/setup.js                      使用 Bun 启动 MCP
 *   deno run -A --no-config --no-lock --node-modules-dir=manual <MCP_DIR>/setup.js
 *   node <MCP_DIR>/setup.js --uninstall         从 Agent 配置移除本集成
 *
 * 流程：选择检索后端（tgrep / CodeGraph / Sirchmunk 三选一）→ 选择下载镜像 → 准备本地依赖
 * （优先复用本地程序，否则 Cargo 构建 / npx 缓存，全部固定版本）→ 全部成功后才写入所选 Agent 的
 * 项目级 MCP 配置。依赖准备失败时不会改动任何 Agent 配置。
 * CodeGraph 同批更新根 AGENTS.md 的条件式提示词；等价正文不重复写入。
 *
 * 可重复执行：用于切换后端、升级固定制品、为更多 Agent 接入或卸载。
 * 常用选项：
 *   --backend=tgrep|codegraph|sirchmunk   指定后端（跳过交互）
 *   --agents=<id1,id2,...>|all  指定要接入的 Agent（跳过交互，--list-agents 查看可选 id；
 *                               all 只含可自动写入项目配置的目标，面板导入类需显式指定）
 *   --mirror=cn|official        指定下载镜像（缺省按地区建议并交互确认）
 *   --offline                   只使用已缓存的制品
 *   --skip-prepare              跳过依赖准备，仅调整 Agent 配置
 *   --uninstall                 移除 Agent 配置中的本集成条目（配合 --agents/--all-agents）
 *   --all-agents                卸载时清理全部 Agent
 *   --yes                       非交互执行（需配合上述显式选项）
 *   --ui=line                   交互菜单用编号行模式（缺省在支持的终端用方向键）
 *   --dry-run                   只打印动作，不写任何文件、不准备依赖、不联网
 *   --help / --list-agents      打印用法 / Agent 列表后退出（无副作用）
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { AgentConfigError, BACKENDS, agentStates, buildAgentOperations, findOpenJournalBatch, productsForTarget, runAgentConfigBatch } from './agents/src/writers.mjs';
import { recoverInterruptedBatch } from './agents/src/fileStore.mjs';
import { SERVER_IDS, autoConfigurableAgents, agentById, agentDefinitions, guidedAgents, targetFor } from './agents/src/registry.mjs';
import { GUIDED_IMPORTS, exportsDirFor, guidedImportGuidance, ideExportStates } from './agents/src/guidance/ideExports.mjs';
import { agentInstallNotes } from './agents/src/agents/index.mjs';
import { createInteractiveUi } from './agents/src/ui/terminalMenu.mjs';
import { runPrepare, writePreparedState } from './common/src/prepare.mjs';
import { NPM_MIRRORS, CARGO_MIRRORS, mirrorSettings, selectMirrors, mirrorSummary } from './common/src/mirrors.mjs';
import { WorkspacePaths, detectWorkspace, projectInfo, validateWorkspaceBuildDir } from './common/src/paths.mjs';
import { currentRuntime, runtimeSupported } from './common/src/runtime.mjs';
import { collectConfig, writeConfig as writeSirchmunkConfig } from './tools/sirchmunk/src/config.mjs';
import { startModelDownload } from './tools/sirchmunk/src/prepare.mjs';

const integrationRoot = path.dirname(fileURLToPath(import.meta.url));

function parseArgv(argv) {
  const options = { help: false, listAgents: false, uninstall: false, allAgents: false, yes: false, dryRun: false, offline: false, skipPrepare: false, backend: null, agents: null, mirror: null, ui: null };
  const requireValue = (flag, value) => {
    if (!value.trim()) {
      throw new Error(`${flag} 需要非空值（--help 查看用法）`);
    }
    return value.trim();
  };
  for (const arg of argv.slice(2)) {
    if (arg === '--help' || arg === '-h') options.help = true;
    else if (arg === '--list-agents') options.listAgents = true;
    else if (arg === '--list-mirrors') options.listMirrors = true;
    else if (arg === '--uninstall') options.uninstall = true;
    else if (arg === '--all-agents') options.allAgents = true;
    else if (arg === '--yes' || arg === '-y') options.yes = true;
    else if (arg === '--dry-run') options.dryRun = true;
    else if (arg === '--offline') options.offline = true;
    else if (arg === '--skip-prepare') options.skipPrepare = true;
    else if (arg.startsWith('--backend=')) options.backend = requireValue('--backend', arg.slice('--backend='.length));
    else if (arg.startsWith('--agents=')) options.agents = requireValue('--agents', arg.slice('--agents='.length));
    else if (arg.startsWith('--mirror=')) options.mirror = requireValue('--mirror', arg.slice('--mirror='.length));
    else if (arg.startsWith('--npm-mirror=')) options.npmMirror = requireValue('--npm-mirror', arg.slice('--npm-mirror='.length));
    else if (arg.startsWith('--cargo-mirror=')) options.cargoMirror = requireValue('--cargo-mirror', arg.slice('--cargo-mirror='.length));
    else if (arg.startsWith('--tgrep-bin=')) options.tgrepBinary = requireValue('--tgrep-bin', arg.slice('--tgrep-bin='.length));
    else if (arg.startsWith('--codegraph-path=')) options.codegraphPath = requireValue('--codegraph-path', arg.slice('--codegraph-path='.length));
    else if (arg.startsWith('--sirchmunk-python=')) options.sirchmunkPython = requireValue('--sirchmunk-python', arg.slice('--sirchmunk-python='.length));
    else if (arg.startsWith('--pip-index-url=')) {
      options.pipIndexURL = requireValue('--pip-index-url', arg.slice('--pip-index-url='.length));
      const url = new URL(options.pipIndexURL);
      if (url.protocol !== 'https:' || url.username || url.password || url.search || url.hash) throw new Error('--pip-index-url 需要不含凭据的 HTTPS 地址');
    }
    else if (arg.startsWith('--llm-base-url=')) options.llmBaseURL = requireValue('--llm-base-url', arg.slice('--llm-base-url='.length));
    else if (arg.startsWith('--llm-model=')) options.llmModel = requireValue('--llm-model', arg.slice('--llm-model='.length));
    else if (arg.startsWith('--repo-root=')) options.repoRoot = requireValue('--repo-root', arg.slice('--repo-root='.length));
    else if (arg.startsWith('--build-dir=')) options.buildDir = requireValue('--build-dir', arg.slice('--build-dir='.length));
    else if (arg.startsWith('--ui=')) {
      options.ui = requireValue('--ui', arg.slice('--ui='.length));
      if (options.ui !== 'line') throw new Error('--ui 仅支持：line');
    }
    else {
      throw new Error(`未知参数：${arg}（--help 查看用法）`);
    }
  }
  if (options.backend && !Object.hasOwn(BACKENDS, options.backend)) {
    throw new Error(`--backend 仅支持：${Object.keys(BACKENDS).join(' | ')}`);
  }
  mirrorSettings({ mirror: options.mirror ?? 'official', npmMirror: options.npmMirror, cargoMirror: options.cargoMirror });
  return options;
}

function printHelp() {
  const usage = [
    '用法：在 Agent 工作区目录执行 node 或 bun <MCP_DIR>/setup.js [选项]',
    'Deno：deno run -A --no-config --no-lock --node-modules-dir=manual <MCP_DIR>/setup.js [选项]',
    'MCP 配置使用本次运行时的绝对路径；Deno 的启动权限参数由安装器生成。',
    '',
    '流程：选择检索后端（tgrep / CodeGraph / Sirchmunk 三选一）→ 选择下载镜像 → 准备本地依赖',
    '（全部固定版本）→ 依赖就绪后把 MCP 服务写入所选 Agent 的项目级配置。',
    '依赖准备失败时不会改动任何 Agent 配置。可重复执行，用于切换后端、升级固定',
    '制品、为更多 Agent 接入或卸载。',
    'CodeGraph 接入会同批更新根 AGENTS.md 的提示词；编码/换行差异不触发重复写入。',
    '',
    '选项：',
    '  --repo-root=<dir>          显式工作区；缺省从执行目录向上检测工程根，未找到时使用执行目录',
    '  --build-dir=<dir>          旧缓存的只读查找目录；新数据统一写入工作区 .mcp-data/',
    '  --backend=tgrep|codegraph|sirchmunk   指定后端（跳过交互）',
    '  --agents=<id1,id2,...>|all  指定要接入的 Agent（跳过交互；--list-agents 查看可选 id；',
    '                              all 只含可自动写入项目配置的目标，面板导入类需显式指定）',
    '  --mirror=cn|official        npm/Cargo 的推荐默认组合；交互时仍各自选择来源',
    '  --npm-mirror=<id>           指定 npm 站点，--list-mirrors 查看选项',
    '  --cargo-mirror=<id>         指定 Cargo 站点（仅 tgrep 源码编译使用）',
    '  --tgrep-bin=<file>          优先使用指定 tgrep 程序（需兼容版本和 stdio 补丁）',
    '  --codegraph-path=<path>     本地 CodeGraph 包、可执行入口或已编译源码目录',
    '  --sirchmunk-python=<file>   Python >= 3.10 可执行文件（缺省优先本地 Python 3.12/3.13）',
    '  --pip-index-url=<url>       Sirchmunk Python 依赖源；默认 PyPI，支持 HTTPS 镜像',
    '  --llm-base-url=<url>        Sirchmunk 的 OpenAI 兼容 LLM baseURL',
    '  --llm-model=<name>          Sirchmunk 模型名；API Key 用隐藏输入或 SIRCHMUNK_LLM_API_KEY',
    '  --offline                   只使用已缓存的制品',
    '  --skip-prepare              跳过依赖准备，仅调整 Agent 配置',
    '  --uninstall                 移除 Agent 配置中的本集成条目（配合 --agents/--all-agents）',
    '  --all-agents                卸载时清理全部 Agent',
    '  --yes                       非交互执行（需配合上述显式选项）',
    '  --ui=line                   交互菜单用编号行模式（缺省在支持的终端用方向键）',
    '  --dry-run                   只打印动作，不写任何文件、不准备依赖、不联网',
    '  --help, -h                  显示本帮助（无副作用）',
    '  --list-agents               列出可配置的 Agent 及其配置文件（无副作用）',
    '  --list-mirrors              列出 npm / Cargo 镜像站点及地址（无副作用）',
    '',
    '安装语义：所选目标构成最终集合——已配置但未选中的托管目标会被移除。',
    '卸载用 --uninstall [--agents=...|--all-agents]；两者都不影响索引与已下载制品。',
  ];
  process.stdout.write(`${usage.join('\n')}\n`);
}

function printAgentList() {
  process.stdout.write('可配置的 Agent（id — 说明 — 仓库内配置文件）：\n');
  for (const agent of agentDefinitions()) {
    const target = targetFor(agent);
    const location = target ? target.file.split(path.sep).join('/')
      : agent.capability === 'export' ? '原生片段导出（需显式加载）' : '面板导入 / 引导（不写项目配置）';
    process.stdout.write(`  ${agent.id.padEnd(14)} ${agent.label}  (${location})\n`);
  }
  process.stdout.write('用 --agents=<id,...> 选择；all 只覆盖可自动写入的目标；docs-only 客户端（Windsurf）见 README 手工配置示例。\n');
}

function printMirrorList() {
  for (const [name, catalog] of [['npm', NPM_MIRRORS], ['Cargo', CARGO_MIRRORS]]) {
    process.stdout.write(name + ' 站点：\n');
    for (const [id, site] of Object.entries(catalog)) process.stdout.write('  ' + id.padEnd(12) + site.label + '  ' + (site.url ?? 'Cargo 现有配置 / 官方默认') + '\n');
  }
  process.stdout.write('国内推荐：npmmirror + rsproxy；选项仅对本次准备生效，不修改全局配置。\n');
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

/**
 * Installer output + menu surface. Menu output always goes through
 * process.stdout directly; the interactive menu adds raw-mode arrow
 * selection with a numbered-line fallback. Non-interactive
 * runs never open an interactive question — missing options fail fast.
 */
function createUi(options) {
  const line = (text = '') => process.stdout.write(`${text}\n`);
  const interactive = !options.yes && !process.env.CI && process.stdin.isTTY && process.stdout.isTTY;
  if (!interactive) {
    const refusal = async () => {
      throw new Error('非交互执行缺少必要选项，请用 --yes 并显式指定 --backend、--agents、--mirror，或在终端中运行');
    };
    return { line, interactive, menu: { singleSelect: refusal, multiSelect: refusal, confirm: refusal, close() {}, mode: 'none' } };
  }
  const menu = createInteractiveUi({ input: process.stdin, output: process.stdout, forceLine: options.ui === 'line' });
  return { line, interactive, menu };
}

function agentEntries(states) {
  return agentDefinitions().map((agent) => {
    const state = states[agent.id] ?? {};
    const note = state.error ? '配置文件异常（相关写入会中止）'
      : !agent.targetId ? (state.present ? '已生成片段（需导入或显式加载）'
        : agent.capability === 'export' ? '原生片段导出（需显式加载）' : '面板导入（不写项目配置）')
      : (state.configured ?? []).length > 0 ? `已配置 ${state.configured.join(', ')}` : '未配置';
    return { key: agent.id, label: agent.label, note };
  });
}

/** Resolve --agents=... to concrete ids; rejects unknown ids before any prompt or download. */
function resolveAgentIds(value) {
  // `all` covers only targets this installer can auto-write; guided-import
  // clients stay opt-in.
  if (!value || value === 'all') {
    return autoConfigurableAgents().map((agent) => agent.id);
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
    const parsed = JSON.parse(fs.readFileSync(paths.preparedStateReadPath(), 'utf8'));
    return parsed && typeof parsed === 'object' && Object.hasOwn(BACKENDS, parsed.backend) ? parsed.backend : null;
  } catch {
    return null;
  }
}

function reportRecovery(ui, recovery) {
  if (recovery) {
    ui.line(`检测到上次运行中断的配置批次，已按备份自动恢复（批次 ${recovery.batchId.slice(0, 8)}…）：`);
    for (const file of recovery.restored) ui.line(`  已还原到写入前内容：${file}`);
    for (const file of recovery.removed) ui.line(`  已删除中断前新建的文件：${file}`);
    for (const skipped of recovery.skipped) ui.line(`  跳过：${skipped.file}（${skipped.reason}）`);
    if (recovery.ownershipRestored !== null) {
      ui.line(recovery.ownershipRestored ? '  归属记录已恢复，文件标识按恢复结果更新。' : '  归属记录未能完整还原，请检查备份。');
    }
    if (recovery.skipped.length || recovery.ownershipRestored === false) ui.line(`  备份目录：${recovery.backupDir}`);
  }
}

function recoverBeforeScan(ui, paths, dryRun) {
  if (dryRun) {
    const open = findOpenJournalBatch({ tmpDir: paths.agentTmpDir });
    if (open) {
      ui.line(`提示：存在上次运行中断、尚未恢复的配置批次（pid ${open.pid}）。--dry-run 只读不执行恢复；实际执行时会先自动恢复。`);
    }
  } else {
    reportRecovery(ui, recoverInterruptedBatch({ repoRoot: paths.repoRoot, stateDir: paths.stateDir, tmpDir: paths.agentTmpDir }));
  }
}

/** Plan/apply a batch, print its actions, and return false on preflight problems. */
function runConfigBatch(ui, paths, operations, { dryRun, ideExports = [] }) {
  const { plan, recovery } = runAgentConfigBatch({
    repoRoot: paths.repoRoot,
    launch: paths.launch,
    operations,
    dryRun,
    stateDir: paths.stateDir,
    tmpDir: paths.agentTmpDir,
    codegraphGuidance: operations.some((op) => op.type === 'configure' && op.backend === 'codegraph') || ideExports.some((op) => op.backend === 'codegraph'),
    ideExports,
  });
  reportRecovery(ui, recovery);
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
    if (step.ideExport) {
      const action = step.action === 'create' || step.action === 'update' ? '生成' : verbs[step.action];
      ui.line(`${step.action === 'unchanged' ? '无变化' : prefix + action} IDE 导入片段：${path.basename(step.file)}`);
      continue;
    }
    if (step.guidance) {
      ui.line(`${step.action === 'unchanged' ? '无变化' : prefix + verbs[step.action]}：${step.relative} 中的 CodeGraph 提示词`);
      continue;
    }
    if (step.legacy) {
      ui.line(`${step.action === 'unchanged' ? '无变化' : `${prefix}清理`}旧格式条目：${step.relative}`);
      continue;
    }
    if (step.consolidation) {
      ui.line(`${step.action === 'unchanged' ? '无变化' : `${prefix}删除`}冗余候选：${step.relative}（外来内容已合并进保留的候选文件）`);
      continue;
    }
    const consumers = productsForTarget(step.target.targetId).map((product) => product.id);
    const merged = step.consolidatedFrom ? `（合并了 ${step.consolidatedFrom.join('、')} 的内容）` : '';
    const label = consumers.length > 1 ? `${step.relative}（${consumers.join(' / ')} 共用，移除会影响全组）${merged}` : `${step.relative}${merged}`;
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
  if (options.uninstall && options.yes && !options.agents && !options.allAgents) {
    throw new Error('--uninstall --yes 需要 --agents=... 或 --all-agents');
  }
  // Fail before preparation when a noninteractive run would need a later menu.
  // --yes retains its documented prepared-backend / existing-selection defaults.
  if (!ui.interactive && !options.yes) {
    const missing = options.uninstall
      ? !options.agents && !options.allAgents
      : !options.backend || !options.agents || (!options.mirror && !(options.npmMirror && (options.backend === 'codegraph' || options.cargoMirror)) && !options.offline && !options.skipPrepare && !options.dryRun);
    if (missing) throw new Error('非交互执行缺少必要选项，请显式指定 --backend、--agents、--mirror，或用 --yes 接受已有默认选择');
  }
  const workspace = detectWorkspace(process.cwd(), { explicit: options.repoRoot });
  const repoRoot = workspace.root;
  const buildDir = options.buildDir ? validateWorkspaceBuildDir(repoRoot, path.resolve(repoRoot, options.buildDir)) : undefined;
  const paths = new WorkspacePaths(repoRoot, buildDir);
  const runtime = currentRuntime();
  if (!runtimeSupported(runtime)) throw new Error('需要 Node.js >= 20、Bun >= 1 或 Deno >= 2');
  paths.launch = { integrationRoot, runtime, ...(options.buildDir ? { buildDir } : {}) };
  const exportsDir = exportsDirFor(paths.integrationDir);
  const scanStates = () => ({ ...agentStates(repoRoot, paths.launch), ...ideExportStates(repoRoot, exportsDir, paths.launch) });

  ui.line(`${projectInfo(repoRoot).name} MCP 集成安装`);
  ui.line(`仓库：${repoRoot}`);
  ui.line(`工作空间依据：${workspace.reason}`);
  ui.line(`MCP 数据目录：${paths.integrationDir}`);
  ui.line(`MCP 运行时：${runtime.kind} ${runtime.version}（${runtime.executable}）`);
  ui.line('');

  // -- 卸载模式（不准备依赖，只读扫描配置） ----------------------------------------
  if (options.uninstall) {
    recoverBeforeScan(ui, paths, options.dryRun);
    const states = scanStates();
    let targets;
    if (options.agents || options.allAgents) {
      targets = options.allAgents && !options.agents ? agentDefinitions().map((agent) => agent.id) : resolveAgentIds(options.agents);
    } else {
      const damaged = Object.entries(states).filter(([, state]) => state.error);
      if (damaged.length) throw new Error(damaged.map(([id, state]) => `${id}: ${state.error}`).join('\n'));
      const current = agentDefinitions().filter((agent) => (states[agent.id]?.configured ?? []).length > 0).map((agent) => agent.id);
      if (current.length === 0) {
        ui.line('当前没有 Agent 配置本集成，无需卸载。');
        return 0;
      }
      const selected = await ui.menu.multiSelect('选择要从哪些 Agent 移除本集成：', agentEntries(states).filter((entry) => (states[entry.key]?.configured ?? []).length > 0), current);
      targets = [...selected];
    }
    // Guided-import products hold no repo config; their delivery is the
    // build-dir snippet removed below.
    const repoTargets = targets.filter((agentId) => agentById(agentId)?.targetId);
    const guidedTargets = targets.filter((agentId) => agentById(agentId) && !agentById(agentId).targetId);
    if (!runConfigBatch(ui, paths, repoTargets.map((agentId) => ({ type: 'remove', agentId })), {
      dryRun: options.dryRun, ideExports: guidedTargets.map((agentId) => ({ agentId, exportsDir })),
    })) {
      return 1;
    }
    ui.line('');
    ui.line('卸载移除项目配置条目与 IDE 导入片段；已手动导入 IDE 的条目需在面板移除。索引与已下载制品保留。');
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
      const index = await ui.menu.singleSelect('选择检索后端（检索工具三选一）：', choices, { defaultIndex: prepared ? Object.keys(BACKENDS).indexOf(prepared) : 0 });
      backend = Object.keys(BACKENDS)[index];
    }
  }

  // Credential validation happens before downloads; dry-run never prompts for secrets.
  const sirchmunkConfig = backend === 'sirchmunk' && !options.dryRun ? await collectConfig(paths, { options, ui }) : null;
  let preparedSirchmunk = null;
  if (backend === 'sirchmunk') ui.line('Sirchmunk 的 FAST/DEEP 会把检索内容发送至配置的 LLM；模型下载在后台进行，就绪后自动启用知识演化。');

  // -- 准备依赖（失败则绝不触碰 Agent 配置；dry-run 完全跳过准备） -------------------
  if (options.dryRun) {
    const mirrors = mirrorSettings({ mirror: options.mirror ?? suggestMirror(), npmMirror: options.npmMirror, cargoMirror: options.cargoMirror });
    ui.line(`dry-run：拟准备依赖（后端：${backend}，${mirrorSummary(mirrors, backend)}），不执行下载、安装或状态写入。`);
  } else if (!options.skipPrepare) {
    const mirrors = await selectMirrors({ options, backend, suggested: suggestMirror(), menu: ui.menu, interactive: ui.interactive });
    const { npmRegistry, cargoConfigArgs, cargoIsolated } = mirrors;
    ui.line('');
    ui.line(`准备依赖（后端：${backend}，${mirrorSummary(mirrors, backend)}）…`);
    try {
      const result = runPrepare({
        repoRoot,
        integrationRoot,
        buildDir,
        backend,
        offline: options.offline,
        npmRegistry,
        cargoConfigArgs,
        cargoIsolated,
        tgrepBinary: options.tgrepBinary,
        codegraphPath: options.codegraphPath,
        sirchmunkPython: options.sirchmunkPython,
        pipIndexURL: options.pipIndexURL,
        log: (message) => ui.line(message),
      });
      result.state.mirrors = { npm: mirrors.npmId, cargo: mirrors.cargoId };
      preparedSirchmunk = result.state.sirchmunk;
      const statePath = writePreparedState(result.paths, result.state);
      ui.line(`状态已写入 ${statePath}`);
    } catch (error) {
      ui.line('');
      ui.line(`依赖准备失败，未修改任何 Agent 配置。`);
      ui.line(`${error.message}`);
      ui.line(`排查建议：检查网络/镜像可达性后重试；或使用同一运行时执行 "${path.join(integrationRoot, 'common/tools/doctor.mjs')}" 查看当前状态（Deno 保留 run 及权限参数）。`);
      return 1;
    }
  } else {
    ui.line('已按 --skip-prepare 跳过依赖准备。');
  }

  if (sirchmunkConfig) writeSirchmunkConfig(paths, sirchmunkConfig);

  // -- 选择 Agent（可多选；已配置的默认保持）——依赖就绪后才扫描配置 ------------------
  recoverBeforeScan(ui, paths, options.dryRun);
  const states = scanStates();
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
    const selected = await ui.menu.multiSelect(`选择要接入的 Agent（${hint}）：`, agentEntries(states), defaults);
    selectedIds = selected;
  }

  // -- 空选择不能静默卸载：先确认再移除 ------------------------------------------------
  if (selectedIds.size === 0) {
    const configuredNow = agentDefinitions().filter((agent) => (states[agent.id]?.configured ?? []).length > 0);
    if (configuredNow.length === 0) {
      ui.line('未选择任何 Agent，结束（未修改 Agent 配置）。');
      return 0;
    }
    if (!ui.interactive) {
      throw new Error('空选择会移除已配置的 Agent 条目；非交互模式请显式使用 --uninstall，或在终端中确认');
    }
    ui.line('');
    ui.line('本轮选择为空。按安装语义（所选目标构成最终集合），以下已配置条目将被移除：');
    for (const agent of configuredNow) {
      ui.line(`  - ${agent.id}（${agent.label}）`);
    }
    if (!(await ui.menu.confirm('确认移除以上全部本集成条目？'))) {
      ui.line('已取消，未修改 Agent 配置；已准备的依赖保留。');
      return 0;
    }
  }

  // -- 两阶段写入：先全量规划（含损坏/候选/迁移检测），全部通过后再统一 apply ----------
  const operations = buildAgentOperations({ states, selectedIds, backend });
  const ideExports = guidedAgents()
    .filter((agent) => selectedIds.has(agent.id) || states[agent.id]?.present)
    .map((agent) => ({ agentId: agent.id, exportsDir, backend: selectedIds.has(agent.id) ? backend : null }));
  if (!runConfigBatch(ui, paths, operations, { dryRun: options.dryRun, ideExports })) {
    return 1;
  }
  if (preparedSirchmunk && selectedIds.size && startModelDownload(paths, preparedSirchmunk, integrationRoot)) {
    ui.line(`Embedding 模型后台下载已启动（无 LLM 调用）；进度：${path.join(preparedSirchmunk.model_dir, 'download-state.json')}`);
  }

  // -- 面板导入类交付：构建目录片段 + 导入步骤（不写项目配置文件） --------------------
  for (const agent of guidedAgents()) {
    if (!selectedIds.has(agent.id)) continue;
    const spec = GUIDED_IMPORTS[agent.id];
    const snippetFile = path.join(exportsDir, spec.snippetFile);
    for (const guidance of guidedImportGuidance({ agentId: agent.id, snippetFile, relativeEntry: path.join(integrationRoot, 'tools', backend, 'src/server.mjs'), dryRun: options.dryRun })) {
      ui.line(guidance);
    }
  }

  const pending = [];
  for (const agentId of selectedIds) {
    const notes = agentInstallNotes(agentId, { repoRoot, integrationRoot, serverIds: Object.values(SERVER_IDS), dryRun: options.dryRun });
    for (const line of notes.guidance) ui.line(line);
    pending.push(...notes.pending);
  }
  for (const agent of guidedAgents()) {
    if (selectedIds.has(agent.id)) {
      pending.push(`${agent.id}：需按上方步骤手动导入或显式加载片段，完成前不算已接入`);
    }
  }
  if (options.agents === 'all') {
    const guidedIds = guidedAgents().map((agent) => agent.id).join(',');
    pending.push(`面板导入类客户端（${guidedIds}）未纳入 all；需要时用 --agents=${guidedIds} 生成导入片段`);
  }
  if (pending.length > 0) {
    ui.line('');
    ui.line(options.dryRun ? '拟执行后的待用户操作：' : '待用户操作：');
    for (const item of pending) {
      ui.line(`  - ${item}`);
    }
  }

  ui.line('');
  if (options.dryRun) {
    ui.line('dry-run 结束：以上动作均未执行（未准备依赖、未写配置、未生成 IDE 片段）。');
  } else {
    ui.line(`完成。后端：${backend}；重启对应的 Agent 客户端后生效。`);
  }
  ui.line(`诊断：node "${path.join(integrationRoot, 'common/tools/doctor.mjs')}" --repo-root "${repoRoot}"`);
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
  if (options.listMirrors) {
    printMirrorList();
    return 0;
  }
  // Unknown agent ids must fail before any prompt, download, or config scan.
  resolveAgentIds(options.agents);
  const ui = createUi(options);
  try {
    return await runSetup(options, ui);
  } finally {
    ui.menu.close();
  }
}

try {
  process.exitCode = await main();
} catch (error) {
  // MenuCancelled (Escape/Ctrl+C/EOF/render failure) already restored the
  // terminal before propagating; agent config remains unchanged, while any
  // dependencies prepared before agent selection are retained.
  process.stderr.write(`setup: ${error.message}\n`);
  if (error instanceof AgentConfigError) {
    process.stderr.write('该配置文件已保持原样（未做任何写入）。请先修复或整理该文件后重试。\n');
  }
  process.exitCode = 1;
}
