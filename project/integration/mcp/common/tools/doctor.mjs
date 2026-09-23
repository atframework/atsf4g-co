#!/usr/bin/env node
/**
 * Read-only diagnostics for the tgrep/CodeGraph/Sirchmunk MCP integrations.
 *
 * Checks the Node version, prepared artifacts, lock state, index directories,
 * and wrapper state records; prints a JSON report. Never mutates anything,
 * never starts a backend, never contacts the network.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

import { WorkspacePaths, detectWorkspace, projectInfo, selectCodegraphIndex, platformName } from '../src/paths.mjs';
import { StateStore, isPidAlive } from '../src/state.mjs';
import { currentRuntime, runtimeSupported } from '../src/runtime.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const MCP_ROOT = path.resolve(HERE, '..', '..');

function check(checks, name, ok, detail = '') {
  checks.push({ check: name, ok: Boolean(ok), detail });
}

function readJson(filePath) {
  try {
    return JSON.parse(fs.readFileSync(filePath, 'utf8'));
  } catch {
    return null;
  }
}

function main() {
  const { values } = parseArgs({
    options: {
      'repo-root': { type: 'string' },
      'build-dir': { type: 'string' },
    },
  });

  const workspace = detectWorkspace(process.cwd(), { explicit: values['repo-root'] });
  const repoRoot = workspace.root;
  const buildDir = values['build-dir'];
  const paths = new WorkspacePaths(repoRoot, buildDir);
  const checks = [];

  const runtime = currentRuntime();
  check(checks, 'wrapper-runtime', runtimeSupported(runtime), `${runtime.kind} ${runtime.version}: ${runtime.executable}`);

  const prepared = readJson(paths.preparedStateReadPath());
  check(checks, 'prepared-state', prepared !== null, prepared ? new Date(prepared.prepared_unix * 1).toISOString() : 'run setup.js');

  for (const tool of ['tgrep', 'codegraph', 'sirchmunk']) {
    const toolDir = paths.toolStateDir(tool);
    const wrapperState = new StateStore(path.join(toolDir, 'wrapper-state.json')).read();
    check(
      checks,
      `${tool}.wrapper-state`,
      true,
      wrapperState ? `${wrapperState.state}${wrapperState.state_detail ? ` (${wrapperState.state_detail})` : ''}` : 'never started'
    );

    const holder = readJson(path.join(toolDir, `${tool}.holder.json`));
    const shared = readJson(path.join(toolDir, 'shared-service.json'));
    const sharedAlive = shared?.state === 'serving' && isPidAlive(shared.pid);
    check(checks, `${tool}.shared-service`, true, sharedAlive
      ? `pid ${shared.pid}, clients=${shared.clients}, ${shared.transport}` : 'no live shared service');
    if (holder?.identity?.pid) {
      check(checks, `${tool}.lock-holder`, sharedAlive && shared.pid === holder.identity.pid,
        `held by pid ${holder.identity.pid} (${holder.stage ?? '?'}, alive=${isPidAlive(holder.identity.pid)})`);
    } else {
      check(checks, `${tool}.lock-holder`, true, 'no live wrapper instance');
    }
    for (const legacy of paths.legacyToolStateDirs(tool)) {
      const file = path.join(legacy, `${tool}.instance.lock`);
      if (!fs.existsSync(file)) continue;
      const record = readJson(path.join(legacy, `${tool}.holder.json`)) ?? readJson(file);
      const pid = record?.identity?.pid;
      check(checks, `${tool}.legacy-lock`, !!pid && !isPidAlive(pid),
        `legacy owner pid ${pid ?? 'unknown'}; close the old Agent before using .mcp-data: ${file}`);
    }
  }

  if (prepared?.sirchmunk) {
    check(checks, 'sirchmunk.python', fs.existsSync(prepared.sirchmunk.python), prepared.sirchmunk.python);
    const model = readJson(path.join(prepared.sirchmunk.model_dir, 'download-state.json'));
    check(checks, 'sirchmunk.embedding', model?.state === 'ready', model?.state ?? 'not downloaded');
    const state = readJson(path.join(paths.toolStateDir('sirchmunk'), 'wrapper-state.json'));
    check(checks, 'sirchmunk.knowledge-evolution', state?.enable_knowledge_evolution === true, state?.state ?? 'never started');
    check(checks, 'sirchmunk.credentials', fs.existsSync(paths.readPath('private/sirchmunk.json')), 'local private configuration (contents omitted)');
  }

  if (prepared?.tgrep?.binary) {
    check(checks, 'tgrep.binary', fs.existsSync(prepared.tgrep.binary), prepared.tgrep.binary);
  }
  const tgrepIndex = path.join(paths.toolStateDir('tgrep'), 'index', 'meta.json');
  check(checks, 'tgrep.index', fs.existsSync(tgrepIndex), tgrepIndex);

  if (prepared?.codegraph?.runtime) {
    check(checks, 'codegraph.runtime', fs.existsSync(prepared.codegraph.runtime), prepared.codegraph.runtime);
    check(checks, 'codegraph.library', fs.existsSync(prepared.codegraph.library_entry), prepared.codegraph.library_entry);
  }
  const selection = selectCodegraphIndex(repoRoot);
  const customIndex = path.join(repoRoot, selection.dirName ?? '.codegraph', 'codegraph.db');
  const defaultIndex = path.join(repoRoot, '.codegraph', 'codegraph.db');
  check(
    checks,
    'codegraph.index',
    fs.existsSync(customIndex) || fs.existsSync(defaultIndex),
    fs.existsSync(defaultIndex) ? defaultIndex : fs.existsSync(customIndex) ? customIndex : 'not initialized yet'
  );

  const extraTools = readJson(path.join(paths.toolStateDir('codegraph'), 'extra-tools.json'));
  check(
    checks,
    'codegraph.extra-tools-config',
    extraTools === null || Array.isArray(extraTools.enabled_tools),
    extraTools ? extraTools.enabled_tools.join(',') : 'default allowlist'
  );

  const report = {
    platform: platformName(),
    repo_root: repoRoot,
    project: projectInfo(repoRoot),
    workspace_detection: workspace.reason,
    data_dir: paths.integrationDir,
    legacy_cache_dirs: paths.legacyIntegrationDirs,
    node: process.version,
    runtime,
    checks,
  };
  process.stdout.write(`${JSON.stringify(report, null, 2)}\n`);
  return 0;
}

try {
  process.exitCode = main();
} catch (error) {
  process.stderr.write(`doctor: ${error.message}\n`);
  process.exitCode = 1;
}
