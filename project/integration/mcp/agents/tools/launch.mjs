#!/usr/bin/env node
/**
 * 显式启动器：为 Cline CLI 以本仓库的 MCP 导出文件启动 Agent。
 *
 * 用法：
 *   node <PROJECT_DIR>/project/integration/mcp/agents/tools/launch.mjs --agent=cline -- [cline 参数...]
 *   node ... --agent=cline --cline <cline 可执行文件或 bin/cline 脚本> -- [cline 参数...]
 *
 * 行为约定：
 * - 仅对子进程设置绝对 CLINE_MCP_SETTINGS_PATH=<repo>/.cline/atsf4g-mcp.json
 *   与仓库 cwd；不修改用户环境，不生成平台 shell 脚本，不在安装时自动启动。
 * - 显式路径替换 Cline 本次进程的默认 MCP 配置源（不合并全局服务器）；
 *   导出文件中的用户补充条目仍会被读取。
 * - 以参数数组调用经验证的 Cline CLI 入口，禁用 shell；Windows 上 npm 的
 *   .cmd/.bat shim 不可用 shell:false 执行，需要改用 --cline 指向
 *   bin/cline 脚本或原生可执行文件。
 */

import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

import { deriveRepoRoot } from '../../common/src/paths.mjs';

const EXPORT_FILE = path.join('.cline', 'atsf4g-mcp.json');
const PLATFORM_PACKAGE_SUFFIX = {
  win32: { platform: 'windows', binary: 'cline.exe' },
  darwin: { platform: 'darwin', binary: 'cline' },
}[process.platform] ?? { platform: 'linux', binary: 'cline' };

function parseArgv(argv) {
  const options = { agent: null, cline: null, passthrough: [] };
  const rest = argv.slice(2);
  let index = 0;
  for (; index < rest.length; index += 1) {
    const argument = rest[index];
    if (argument === '--') {
      index += 1;
      break;
    }
    if (argument.startsWith('--agent=')) options.agent = argument.slice('--agent='.length);
    else if (argument === '--cline') {
      index += 1;
      if (index >= rest.length || !rest[index].trim() || rest[index].startsWith('--')) throw new Error('--cline 需要一个路径参数');
      options.cline = rest[index];
    } else if (argument.startsWith('--cline=')) {
      options.cline = argument.slice('--cline='.length);
      if (!options.cline.trim()) throw new Error('--cline 需要一个路径参数');
    }
    else if (argument === '--help' || argument === '-h') options.help = true;
    else throw new Error(`未知参数：${argument}（--help 查看用法）`);
  }
  options.passthrough = rest.slice(index);
  return options;
}

function printHelp() {
  const usage = [
    '用法：node project/integration/mcp/agents/tools/launch.mjs --agent=cline -- [cline 参数...]',
    '',
    '为 Cline CLI 设置 CLINE_MCP_SETTINGS_PATH=<仓库>/.cline/atsf4g-mcp.json 后启动，',
    'cwd 固定为仓库根。该显式路径替换 Cline 本次进程的默认 MCP 配置源，不合并全局服务器。',
    '',
    '选项：',
    '  --agent=cline   目前仅支持 cline',
    '  --cline <path>  显式指定 cline 入口（bin/cline 脚本或原生可执行文件；不能是 .cmd/.bat shim）',
    '  --help, -h      显示本帮助',
    '',
    'Cline CLI 入口解析顺序：--cline > CLINE_BIN_PATH > npm 全局 cline/bin/cline 脚本 >',
    'npm 全局平台包原生二进制 > 报错并给出 npm install -g cline 提示。',
  ];
  process.stdout.write(`${usage.join('\n')}\n`);
}

function fail(message) {
  process.stderr.write(`launch: ${message}\n`);
  process.exitCode = 1;
}

/** Common npm global node_modules locations, without spawning npm. */
function npmGlobalRootCandidates() {
  const executableDirectory = path.dirname(process.execPath);
  const candidates = [];
  if (process.env.npm_config_prefix) {
    candidates.push(process.platform === 'win32'
      ? path.join(process.env.npm_config_prefix, 'node_modules')
      : path.join(process.env.npm_config_prefix, 'lib', 'node_modules'));
  }
  if (process.platform === 'win32') {
    if (process.env.APPDATA) candidates.push(path.join(process.env.APPDATA, 'npm', 'node_modules'));
    candidates.push(path.join(executableDirectory, 'node_modules'));
  } else {
    candidates.push(path.join(executableDirectory, '..', 'lib', 'node_modules'));
    candidates.push('/usr/local/lib/node_modules');
    if (process.env.HOME) {
      candidates.push(path.join(process.env.HOME, '.npm-global', 'lib', 'node_modules'));
      candidates.push(path.join(process.env.HOME, '.npm', 'global', 'lib', 'node_modules'));
    }
    candidates.push('/opt/homebrew/lib/node_modules');
  }
  return [...new Set(candidates)];
}

/**
 * npm `.cmd`/`.bat` shims need a shell and are rejected on every platform —
 * a Windows-style path passed from WSL would fail the same way.
 */
function isWindowsShellShim(file) {
  return /\.(cmd|bat)$/i.test(file);
}

/** JS entry files run under the current Node; native binaries spawn directly. */
function isJavaScriptEntry(file) {
  if (/\.(js|mjs|cjs)$/i.test(file)) return true;
  const fd = fs.openSync(file, 'r');
  try {
    const prefix = Buffer.alloc(256);
    const length = fs.readSync(fd, prefix, 0, prefix.length, 0);
    return /^#![^\r\n]*\bnode(?:\s|$)/.test(prefix.subarray(0, length).toString('utf8').split(/\r?\n/, 1)[0]);
  } finally { fs.closeSync(fd); }
}

function entryAt(file, source) {
  const resolved = path.resolve(file);
  if (isWindowsShellShim(resolved)) {
    throw new Error(`${source}: ${file} 是 npm 的 shell shim，无法在禁用 shell 的启动器中执行；请指向 cline 包的 bin/cline 脚本或原生可执行文件`);
  }
  if (!fs.statSync(resolved, { throwIfNoEntry: false })?.isFile()) {
    throw new Error(`${source} 指向的文件不存在或不是普通文件：${file}`);
  }
  return isJavaScriptEntry(resolved)
    ? { command: process.execPath, args: [resolved] }
    : { command: resolved, args: [] };
}

/**
 * Resolve the verified Cline CLI entry. Returns {command, args} where args
 * are the leading arguments (the resolver script runs under Node).
 */
function resolveClineEntry(explicit) {
  if (explicit) {
    return entryAt(explicit, '--cline');
  }
  const environmentOverride = process.env.CLINE_BIN_PATH?.trim();
  if (environmentOverride) {
    return entryAt(environmentOverride, 'CLINE_BIN_PATH');
  }
  for (const root of npmGlobalRootCandidates()) {
    const resolver = path.join(root, 'cline', 'bin', 'cline');
    if (fs.existsSync(resolver)) {
      return { command: process.execPath, args: [resolver] };
    }
  }
  for (const root of npmGlobalRootCandidates()) {
    const binary = path.join(root, `@cline/cli-${PLATFORM_PACKAGE_SUFFIX.platform}-${process.arch}`, 'bin', PLATFORM_PACKAGE_SUFFIX.binary);
    if (fs.existsSync(binary)) {
      return { command: binary, args: [] };
    }
  }
  throw new Error(`未找到 Cline CLI 入口。先安装：npm install -g cline；或用 --cline 显式指定入口。已探测的全局目录：${npmGlobalRootCandidates().join('; ')}`);
}

function main() {
  const options = parseArgv(process.argv);
  if (options.help) {
    printHelp();
    return;
  }
  if (options.agent !== 'cline') {
    fail(`--agent 目前仅支持 cline（收到：${options.agent ?? '未提供'}）`);
    return;
  }
  const repoRoot = deriveRepoRoot(new URL(import.meta.url), null);
  const exportPath = path.join(repoRoot, EXPORT_FILE);
  if (!fs.existsSync(exportPath)) {
    fail(`未找到 ${EXPORT_FILE}；先运行 node project/integration/mcp/setup.js 并选择 cline。`);
    return;
  }
  let entry;
  try {
    entry = resolveClineEntry(options.cline);
  } catch (error) {
    fail(error.message);
    return;
  }
  const childEnv = { ...process.env, CLINE_MCP_SETTINGS_PATH: exportPath };
  // The npm bin/cline resolver also consumes this override. We have already
  // resolved it; forwarding it can bypass --cline or recurse into the script.
  delete childEnv.CLINE_BIN_PATH;
  const result = spawnSync(entry.command, [...entry.args, ...options.passthrough], {
    cwd: repoRoot,
    stdio: 'inherit',
    shell: false,
    env: childEnv,
  });
  if (result.error) {
    fail(`无法启动 Cline CLI（${entry.command}）：${result.error.message}`);
    return;
  }
  process.exitCode = result.status ?? 1;
}

try { main(); } catch (error) { fail(error.message); }
