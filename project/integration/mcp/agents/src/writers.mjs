/**
 * Orchestration for agent config writing: batch plan/apply over the pure
 * planner and the safe file store, plus the single-agent compatibility API
 * that common/src/agents.mjs re-exports (Plan.md 11.2).
 *
 * A batch either plans completely and applies, or plans and reports problems
 * without touching anything. Applying rolls back already-written files from
 * backups when a later file fails (report included; not a cross-file
 * transaction).
 */

import fs from 'node:fs';
import path from 'node:path';

import { resolveBuildDir, WorkspacePaths } from '../../common/src/paths.mjs';
import { AgentConfigError } from './errors.mjs';
import { planConfigChanges } from './configPlan.mjs';
import { createFileStore, readConfigFile } from './fileStore.mjs';
import { planCodegraphGuidance } from './guidance/codegraph.mjs';
import { planIdeExport } from './guidance/ideExports.mjs';
import * as jsonDocument from './formats/jsonDocument.mjs';
import * as codexToml from './formats/codexToml.mjs';
import { BACKENDS, SERVER_IDS, managedServerIds } from './backends.mjs';
import { agentById, agentDefinitions, legacyLocations, productsForTarget, targetFor } from './agents/index.mjs';

export { AgentConfigError };
export { BACKENDS, SERVER_IDS, agentById, agentDefinitions, productsForTarget, targetFor, managedServerIds };
export function allManagedServerIds() {
  return managedServerIds();
}

function defaultDirs(repoRoot, { stateDir, tmpDir } = {}) {
  if (stateDir && tmpDir) {
    return { stateDir, tmpDir };
  }
  const paths = new WorkspacePaths(repoRoot, resolveBuildDir(repoRoot, null));
  return { stateDir: stateDir ?? paths.stateDir, tmpDir: tmpDir ?? paths.agentTmpDir };
}

/** Managed ids referenced by one JSON config file, or null when unreadable/damaged. */
function jsonConfiguredIds(text, format, filePath) {
  const document = jsonDocument.parseJsonDocument(text, filePath);
  const map = jsonDocument.walkServerMap(document.root, format, filePath);
  return map === null ? [] : managedServerIds().filter((id) => Object.prototype.hasOwnProperty.call(map, id));
}

/**
 * Which managed servers does each agent config currently reference? All
 * existing candidates and legacy locations of the product's target are
 * scanned and unioned; several existing exclusive candidates are no longer an
 * error — configure runs consolidate them (Plan.md 11.17) — while damaged
 * files still surface as an error.
 */
export function agentStates(repoRoot) {
  const states = {};
  for (const agent of agentDefinitions()) {
    const target = targetFor(agent);
    const state = { configured: [], present: false };
    states[agent.id] = state;
    if (!target) continue; // guided-import products never hold repo config
    const problems = [];
    const candidates = target.candidates ?? [target.file];
    const existing = candidates.filter((candidate) => fs.existsSync(path.join(repoRoot, candidate)));
    // Legacy descriptors carry their own format; missing locations are normal.
    const scanFiles = [
      ...existing.map((relative) => ({ relative, format: target.format })),
      ...legacyLocations(target).map(({ file, format }) => ({ relative: file, format })),
    ];
    for (const { relative, format } of scanFiles) {
      const filePath = path.join(repoRoot, relative);
      try {
        const text = readConfigFile(repoRoot, filePath);
        if (text === null) continue;
        state.present = true;
        if (format === 'codexToml') {
          const ids = codexToml.codexManagedTables(text);
          for (const id of managedServerIds()) {
            if (ids.has(id) && !state.configured.includes(id)) {
              state.configured.push(id);
            }
          }
        } else {
          for (const id of jsonConfiguredIds(text, format, filePath)) {
            if (!state.configured.includes(id)) {
              state.configured.push(id);
            }
          }
        }
      } catch (error) {
        problems.push(error instanceof AgentConfigError ? error.message : String(error?.message ?? error));
      }
    }
    if (problems.length > 0) {
      state.configured = [];
      state.error = problems.join('; ');
    }
  }
  return states;
}

/**
 * Operations for a setup run: selected products configure their target; a
 * configured-but-unselected product is removed only when NO product of its
 * shared target group is selected (selecting any member keeps the group).
 * Targetless (guided-import) products produce no repo operations here —
 * their delivery (snippets, guidance) is handled by the installer.
 */
export function buildAgentOperations({ states, selectedIds, backend }) {
  const keepTargets = new Set(agentDefinitions().filter((agent) => agent.targetId && selectedIds.has(agent.id)).map((agent) => agent.targetId));
  const operations = [];
  for (const agent of agentDefinitions()) {
    if (!agent.targetId) continue;
    if (selectedIds.has(agent.id)) {
      operations.push({ type: 'configure', agentId: agent.id, backend });
    } else if (((states[agent.id]?.configured ?? []).length > 0 || states[agent.id]?.error) && !keepTargets.has(agent.targetId)) {
      operations.push({ type: 'remove', agentId: agent.id, reason: 'unselected' });
    }
  }
  return operations;
}

export function planAgentConfigChanges({ repoRoot, operations, stateDir, tmpDir, codegraphGuidance = false, ideExports = [] }) {
  const dirs = defaultDirs(repoRoot, { stateDir, tmpDir });
  const store = createFileStore({ repoRoot, stateDir: dirs.stateDir, tmpDir: dirs.tmpDir });
  const plan = planConfigChanges({ repoRoot, operations, readFile: store.readFile, ownsFile: store.owns });
  if (codegraphGuidance) {
    try { plan.steps.push(planCodegraphGuidance({ repoRoot, readBuffer: store.readBuffer })); }
    catch (error) { plan.problems.push({ targetId: 'codegraph-guidance', agents: [], error }); }
  }
  for (const options of ideExports) {
    try { plan.steps.push(planIdeExport({ ...options, repoRoot, readFile: store.readFile })); }
    catch (error) { plan.problems.push({ targetId: 'ide-export', agents: [options.agentId], error }); }
  }
  return plan;
}

export function applyAgentConfigChanges({ repoRoot, plan, stateDir, tmpDir }) {
  if (plan.problems && plan.problems.length > 0) {
    throw plan.problems[0].error;
  }
  const dirs = defaultDirs(repoRoot, { stateDir, tmpDir });
  const store = createFileStore({ repoRoot, stateDir: dirs.stateDir, tmpDir: dirs.tmpDir });
  const results = [];
  try {
    for (const step of plan.steps) {
      // Cleanup depends on the destination still holding the migrated data,
      // including when the primary step was unchanged and skipped its write.
      if (step.destinationGuard && store.readFile(step.destinationGuard.file) !== step.destinationGuard.expected) {
        throw new AgentConfigError(`${step.destinationGuard.relative}: migration destination changed before source cleanup`, 'concurrent-modification');
      }
      if (step.action === 'unchanged') {
        results.push({ step, action: 'unchanged' });
        continue;
      }
      const action = step.deleteFile
        ? store.remove(step.file, { relative: step.relative, expectedBefore: step.before })
        : store.write(step.file, step.after, { relative: step.relative, expectedBefore: step.before });
      results.push({ step, action });
    }
  } catch (error) {
    const rollback = plan.steps.filter((step) => store.hasMutation(step.file)).reverse().map((step) => {
      try { return { relative: step.relative, restored: store.tryRestore(step) }; }
      catch (restoreError) { return { relative: step.relative, restored: false, error: restoreError.message }; }
    });
    const failure = new Error(
      `配置写入失败，已回滚 ${rollback.filter((entry) => entry.restored).length}/${rollback.length} 个已写文件：${error.message}`,
    );
    failure.name = 'AgentBatchError';
    failure.rollback = rollback;
    failure.cause = error;
    failure.message += rollback.filter((entry) => !entry.restored).map((entry) => `\n未恢复：${entry.relative}${entry.error ? `（${entry.error}）` : ''}`).join('');
    if (rollback.some((entry) => !entry.restored)) failure.message += `\n备份目录：${store.backupDir}`;
    throw failure;
  }
  return results;
}

/** Plan a batch; apply it unless dry-run. Returns { plan, applied }; problems are in plan.problems. */
export function runAgentConfigBatch({ repoRoot, operations, dryRun = false, stateDir, tmpDir, codegraphGuidance = false, ideExports = [] }) {
  const plan = planAgentConfigChanges({ repoRoot, operations, stateDir, tmpDir, codegraphGuidance, ideExports });
  if (plan.problems.length > 0) {
    return { plan, applied: false };
  }
  if (!dryRun) {
    applyAgentConfigChanges({ repoRoot, plan, stateDir, tmpDir });
  }
  return { plan, applied: !dryRun };
}

const COMPAT_ACTIONS = { create: 'created', update: 'updated', delete: 'removed-file', unchanged: 'unchanged' };

/** True when at least one step of the plan would change something on disk. */
function planChangesSomething(plan) {
  return plan.steps.some((step) => step.action !== 'unchanged');
}

/** The first step that changes something (primary or legacy), for compat result shapes. */
function firstChangingStep(plan) {
  return plan.steps.find((step) => step.action !== 'unchanged') ?? plan.steps[0];
}

/**
 * Write (or replace) our backend entry in one agent's config file (compat
 * single-agent API; implemented on top of plan+apply).
 *
 * @returns {{agent: object, file: string, action: 'created'|'updated'|'removed-file'|'unchanged'}}
 */
export function configureAgent({ repoRoot, agentId, backend, dryRun = false, stateDir, tmpDir }) {
  const agent = agentById(agentId);
  if (!agent) {
    throw new Error(`unknown agent ${agentId}`);
  }
  if (!agent.targetId) {
    throw new Error(`${agent.id} has no project config file; its delivery is guidance only`);
  }
  const plan = planAgentConfigChanges({ repoRoot, operations: [{ type: 'configure', agentId, backend }], stateDir, tmpDir });
  if (plan.problems.length > 0) {
    throw plan.problems[0].error;
  }
  const step = firstChangingStep(plan);
  if (!dryRun && planChangesSomething(plan)) {
    applyAgentConfigChanges({ repoRoot, plan, stateDir, tmpDir });
  }
  return { agent, file: step.file, action: COMPAT_ACTIONS[step.action] };
}

/**
 * Remove every managed server entry from one agent's config file (compat
 * single-agent API). Files we created and that end up as an empty managed
 * skeleton are deleted; legacy locations with our entries are cleaned too.
 *
 * @returns {{agent: object, file: string, action: 'updated'|'removed-file'|'unchanged'}}
 */
export function removeAgentServers({ repoRoot, agentId, dryRun = false, stateDir, tmpDir }) {
  const agent = agentById(agentId);
  if (!agent) {
    throw new Error(`unknown agent ${agentId}`);
  }
  if (!agent.targetId) {
    throw new Error(`${agent.id} has no project config file; its delivery is guidance only`);
  }
  const plan = planAgentConfigChanges({ repoRoot, operations: [{ type: 'remove', agentId }], stateDir, tmpDir });
  if (plan.problems.length > 0) {
    throw plan.problems[0].error;
  }
  const step = firstChangingStep(plan);
  if (!dryRun && planChangesSomething(plan)) {
    applyAgentConfigChanges({ repoRoot, plan, stateDir, tmpDir });
  }
  return { agent, file: step.file, action: COMPAT_ACTIONS[step.action] };
}
