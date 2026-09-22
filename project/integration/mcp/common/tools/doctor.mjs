#!/usr/bin/env node
/**
 * Read-only diagnostics for the tgrep/CodeGraph MCP integrations.
 *
 * Checks the Node version, prepared artifacts, lock state, index directories,
 * and wrapper state records; prints a JSON report. Never mutates anything,
 * never starts a backend, never contacts the network.
 */

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';

import { WorkspacePaths, detectWorkspace, projectInfo, selectCodegraphIndex, platformName, resolveBuildDir } from '../src/paths.mjs';
import { StateStore, isPidAlive } from '../src/state.mjs';

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
  const buildDir = resolveBuildDir(repoRoot, values['build-dir']);
  const paths = new WorkspacePaths(repoRoot, buildDir);
  const checks = [];

  check(checks, 'node>=20', Number(process.versions.node.split('.')[0]) >= 20, process.version);

  const prepared = readJson(paths.preparedStatePath());
  check(checks, 'prepared-state', prepared !== null, prepared ? new Date(prepared.prepared_unix * 1).toISOString() : 'run common/tools/prepare.mjs');

  for (const tool of ['tgrep', 'codegraph']) {
    const toolDir = paths.toolStateDir(tool);
    const wrapperState = new StateStore(path.join(toolDir, 'wrapper-state.json')).read();
    check(
      checks,
      `${tool}.wrapper-state`,
      true,
      wrapperState ? `${wrapperState.state}${wrapperState.state_detail ? ` (${wrapperState.state_detail})` : ''}` : 'never started'
    );

    const holder = readJson(path.join(toolDir, `${tool}.holder.json`));
    if (holder?.identity?.pid) {
      check(checks, `${tool}.lock-holder`, false, `held by pid ${holder.identity.pid} (${holder.stage ?? '?'}, alive=${isPidAlive(holder.identity.pid)})`);
    } else {
      check(checks, `${tool}.lock-holder`, true, 'no live wrapper instance');
    }
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
    build_dir: buildDir,
    node: process.version,
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
