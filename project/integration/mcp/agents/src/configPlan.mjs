/**
 * Pure change planning for agent config files (no I/O of its own).
 *
 * planConfigChanges receives injectable `readFile` / `ownsFile` accessors and
 * groups operations by TARGET (several products may share one physical file).
 * Per target it resolves the effective candidate file, computes the primary
 * edit, and appends cleanup steps that run only after the primary write
 * succeeded (see the migration rules in README 候选文件与旧格式迁移):
 *
 * - Same-directory JSON/JSONC candidates combine compatible nested fields
 *   and preserve comments, BOM and EOL. Conflicting leaves (including managed
 *   options) abort the batch. Different directories remain separate layers;
 *   only verified managed entries migrate between them. Redundant files are
 *   deleted after the merged destination write, with backup and rollback.
 *   Removal-only batches instead edit every existing candidate in place.
 * - Legacy cleanup removes only managed entries that verifiably point at this
 *   repository's wrappers (server id alone never claims a user-built server).
 * - The deprecated root `mcp.json` (CodeBuddy) is edited in place only when
 *   CodeBuddy is the sole selected consumer; otherwise creating `.mcp.json`
 *   must not shadow foreign servers in it — that case is a conflict until a
 *   previewed migration exists.
 *
 * A target that cannot be resolved or parsed safely becomes a problem entry
 * instead of a step, so a batch with problems writes nothing.
 */

import path from 'node:path';

import { AgentConfigError } from './errors.mjs';
import { serverEntry, isOurServerEntry, entryExtraArgs, configuredServerIds } from './entries.mjs';
import { REGISTRY, TARGETS, legacyLocations } from './agents/index.mjs';
import { BACKENDS, managedServerIds, backendForServerId } from './backends.mjs';
import * as jsonDocument from './formats/jsonDocument.mjs';
import * as codexToml from './formats/codexToml.mjs';
import { appendMigratedComments, mergeCandidateDocuments, mergeJsonValues, removedComments } from './migration/jsonMerge.mjs';
import { hasCustomOptions, migrateLegacyEntry } from './migration/legacyEntry.mjs';

export function planConfigChanges({ repoRoot, operations, readFile, ownsFile = () => false, registry = REGISTRY, targets = TARGETS, launch = {} }) {
  const groups = new Map();
  for (const op of operations) {
    if (!['configure', 'remove'].includes(op.type) || (op.type === 'configure' && !Object.hasOwn(BACKENDS, op.backend))) {
      throw new AgentConfigError('invalid agent operation or backend', 'invalid-operation');
    }
    const agent = registry.find((entry) => entry.id === op.agentId || entry.aliases.includes(op.agentId));
    if (!agent) {
      throw new Error(`unknown agent ${op.agentId}`);
    }
    const target = targets[agent.targetId];
    let group = groups.get(agent.targetId);
    if (!group) {
      group = { targetId: agent.targetId, target, agents: new Set(), ops: [] };
      groups.set(agent.targetId, group);
    }
    group.agents.add(agent.id);
    group.ops.push({ ...op, agent });
  }

  const steps = [];
  const problems = [];
  const notes = [];
  for (const group of groups.values()) {
    try {
      const planned = planTarget({ repoRoot, group, readFile, ownsFile, launch });
      steps.push(...planned.steps);
      notes.push(...planned.notes);
    } catch (error) {
      problems.push({ targetId: group.targetId, agents: [...group.agents], error });
    }
  }
  return { steps, problems, notes };
}

/** Resolve the effective candidate file for a target (see module doc). Returns the file, its current text (null = missing), and legacy files needing cleanup. Multi-existing exclusive candidates are handled by the consolidation path in planTarget and never reach here. */
function resolveTargetFile({ repoRoot, target, group, contents, existing, launch }) {
  if ((target.candidatesMode ?? 'single') === 'priority' && target.legacyFallback && existing.length === 1 && existing[0] === target.legacyFallback.candidate) {
    const selected = group.ops.filter((op) => op.type === 'configure').map((op) => op.agent.id);
    const soleLegacyOwner = selected.length > 0 && selected.every((id) => target.legacyFallback.inPlaceOwnerIds.includes(id));
    const legacyRoot = jsonDocument.parseJsonDocument(contents.get(existing[0]), existing[0]).root;
    const map = jsonDocument.walkServerMap(legacyRoot, target.format, existing[0]) ?? {};
    const hasForeign = Object.keys(map).some((id) => !configuredServerIds(map, repoRoot, launch).includes(id) || !isOurServerEntry(map[id], repoRoot, launch));
    if (soleLegacyOwner || selected.length === 0) {
      return { file: existing[0], before: contents.get(existing[0]), legacyCleanups: [] };
    }
    if (hasForeign) {
      throw new AgentConfigError(
        `legacy ${existing[0]} has non-managed servers; creating ${target.file} would shadow them for clients still reading the legacy file (previewed migration not implemented)`,
        'legacy-shadow-conflict',
      );
    }
    return { file: target.file, before: null, legacyCleanups: [existing[0]] };
  }

  return { file: existing[0] ?? target.file, before: existing.length > 0 ? contents.get(existing[0]) : null, legacyCleanups: [] };
}

function planTarget({ repoRoot, group, readFile, ownsFile, launch }) {
  const target = { ...group.target, targetId: group.targetId };
  const candidates = target.candidates ?? [target.file];
  const contents = new Map(candidates.map((candidate) => [candidate, readFile(path.join(repoRoot, candidate))]));
  const existing = candidates.filter((candidate) => contents.get(candidate) !== null);
  const configuring = group.ops.some((op) => op.type === 'configure');
  const backends = new Set(group.ops.filter((op) => op.type === 'configure').map((op) => op.backend));
  if (backends.size > 1) throw new AgentConfigError('conflicting backends requested for one file', 'conflict');
  const backend = [...backends][0];
  const steps = [];
  const notes = [];
  const legacy = legacyLocations(target, repoRoot).slice();
  let effective;
  let before;
  let input;
  let sources = [];
  if (target.candidatesMode === 'exclusive') {
    if (!configuring) {
      if (existing.length === 0) {
        effective = target.file;
        before = input = null;
      }
      for (const file of existing) {
        steps.push(planFileEdit({ repoRoot, relative: file.split(path.sep).join('/'), filePath: path.join(repoRoot, file), target, group, before: contents.get(file), ownsFile, launch }));
      }
    } else {
      // Different directories are separate upstream configuration layers.
      // Prefer the declared target's layer when it already exists; otherwise
      // edit the existing layer in place. Consolidate only within that layer.
      const directory = existing.some((file) => path.dirname(file) === path.dirname(target.file))
        ? path.dirname(target.file) : path.dirname(existing[0] ?? target.file);
      const sameLayer = existing.filter((file) => path.dirname(file) === directory);
      effective = sameLayer[0] ?? target.file;
      before = contents.get(effective) ?? null;
      sources = sameLayer.slice(1);
      input = sources.length ? mergeCandidateDocuments({ target, destination: effective, sources, contents }) : before;
      for (const file of existing.filter((file) => path.dirname(file) !== directory)) legacy.push({ file, format: target.format });
    }
  } else {
    const resolved = resolveTargetFile({ repoRoot, target, group, contents, existing, launch });
    effective = resolved.file;
    before = resolved.before;
    input = before;
    for (const file of resolved.legacyCleanups) legacy.push({ file, format: target.format });
  }

  // Prepare migrations before computing the final destination edit. Cleanup
  // steps stay after that write in the same guarded, rollback-capable batch.
  const cleanupSteps = [];
  for (const { file, format } of legacy) {
    const relative = file.split(path.sep).join('/');
    const step = planLegacyCleanup({
      repoRoot, format, configuring, relative, filePath: path.join(repoRoot, file), readFile, ownsFile, notes, launch,
      migrate(id, entry) {
        if (backendForServerId(id) !== backend) {
          if (hasCustomOptions(entry, repoRoot, launch)) throw new AgentConfigError(`${relative}: ${id} has custom settings for a different backend; migrate them manually (no files written)`, 'legacy-options-conflict');
          return;
        }
        const migrated = migrateLegacyEntry({ entry, from: format, to: target.format, repoRoot, backend, relative, launch });
        let document = jsonDocument.parseJsonDocument(input ?? '{\n}\n', effective);
        const destinationMap = jsonDocument.walkServerMap(document.root, target.format, effective);
        const destinationEntry = destinationMap?.[BACKENDS[backend].serverId];
        if (destinationEntry && isOurServerEntry(destinationEntry, repoRoot, launch)) {
          const normalized = migrateLegacyEntry({ entry: destinationEntry, from: target.format, to: target.format, repoRoot, backend, relative: effective, launch });
          document = jsonDocument.parseJsonDocument(jsonDocument.upsertServerEntry(document.text, target.format, BACKENDS[backend].serverId, normalized), effective);
        }
        const mapKey = jsonDocument.serverMapRootKey(target.format);
        const merged = mergeJsonValues(document.text, { [mapKey]: { [BACKENDS[backend].serverId]: migrated } }, { destination: effective, source: relative });
        input = `${document.hadBom ? '\uFEFF' : ''}${merged}`;
        notes.push(`${relative}: 将 ${id} 的兼容选项迁移到 ${effective}，成功写入后清理旧条目`);
      },
    });
    if (step) {
      if (configuring && target.copyLegacyRemainder && step.after !== null) {
        // Explicit-load exports do not merge another file at runtime. Carry
        // its remaining user entries into the new export before switching paths.
        input = mergeJsonValues(input ?? '{\n}\n', jsonDocument.parseJsonDocument(step.after, relative).root,
          { destination: effective, source: relative });
      }
      if (configuring) input = appendMigratedComments(input ?? '{\n}\n', relative, removedComments(step.before, step.after));
      cleanupSteps.push(step);
    }
  }
  if (effective !== undefined) {
    const primary = planFileEdit({ repoRoot, relative: effective.split(path.sep).join('/'), filePath: path.join(repoRoot, effective), target, group, before: input, ownsFile, launch });
    // Both the action and concurrency/rollback bytes must refer to the real
    // destination, not the temporary merged document used as edit input.
    primary.before = before;
    if (!primary.deleteFile) primary.action = primary.after === before ? 'unchanged' : before === null ? 'create' : 'update';
    if (sources.length) primary.consolidatedFrom = sources.map((file) => file.split(path.sep).join('/'));
    steps.push(primary);
    const destinationGuard = { file: primary.file, relative: primary.relative, expected: primary.after };
    for (const source of sources) {
      steps.push({ target, relative: source.split(path.sep).join('/'), file: path.join(repoRoot, source), agents: [...group.agents], ops: group.ops,
        before: contents.get(source), after: null, action: 'delete', deleteFile: true, consolidation: true, destinationGuard });
    }
    if (configuring) for (const step of cleanupSteps) step.destinationGuard = destinationGuard;
    if (sources.length) notes.push(`${effective}: 将合并 ${sources.join('、')} 的兼容字段和注释；写入成功后删除来源文件（有备份，可回滚）`);
  }
  steps.push(...cleanupSteps);
  return { steps, notes };
}

function planFileEdit({ repoRoot, relative, filePath, target, group, before, ownsFile, launch }) {
  const format = target.format;
  const backends = new Set(group.ops.filter((op) => op.type === 'configure').map((op) => op.backend));
  if (backends.size > 1) {
    throw new AgentConfigError(`${relative}: conflicting backends (${[...backends].join(', ')}) requested for one file`, 'conflict');
  }
  const backend = backends.size === 1 ? [...backends][0] : null;

  let after;
  if (format === 'codexToml') {
    const normalized = before !== null ? codexToml.assertCodexServers(before, repoRoot, launch) : null;
    after = backend ? codexToml.configureCodexToml(normalized, repoRoot, backend, launch)
      : normalized === null ? null : codexToml.removeCodexTomlServers(normalized).text;
  } else {
    let hadBom = false;
    if (before === null) {
      after = null;
    } else {
      let normalized = before;
      const oldMap = jsonDocument.walkServerMap(jsonDocument.parseJsonDocument(before, filePath).root, format, filePath);
      for (const id of configuredServerIds(oldMap, repoRoot, launch)) {
        if (!managedServerIds().includes(id)) normalized = jsonDocument.renameServerEntry(normalized, format, id, BACKENDS[backendForServerId(id)].serverId);
      }
      const document = jsonDocument.parseJsonDocument(normalized, filePath);
      const map = jsonDocument.walkServerMap(document.root, format, filePath);
      after = document.text;
      hadBom = document.hadBom;
      // Keep the selected entry in place; field edits preserve user options/comments.
      for (const id of managedServerIds()) {
        if (map && Object.hasOwn(map, id) && !isOurServerEntry(map[id], repoRoot, launch)) {
          throw new AgentConfigError(`${relative}: ${id} does not point at this repository's wrapper; preserved without changes`, 'server-conflict');
        }
        if (id !== BACKENDS[backend]?.serverId && map !== null && Object.prototype.hasOwnProperty.call(map, id)) {
          after = jsonDocument.removeServerEntry(after, format, id);
        }
      }
    }
    if (backend) {
      // jsonc modify cannot delete in an empty document, so a fresh file is
      // seeded only when there is an entry to add.
      const entry = serverEntry(format, repoRoot, backend, launch);
      const existing = after && jsonDocument.walkServerMap(jsonDocument.parseJsonDocument(after, filePath).root, format, filePath)?.[BACKENDS[backend].serverId];
      // Product defaults fill absent fields; an explicit client preference wins.
      for (const [key, value] of Object.entries(target.entryDefaults ?? {})) {
        if (!existing || !Object.hasOwn(existing, key)) entry[key] = value;
      }
      if (existing && Object.hasOwn(existing, 'enabled')) entry.enabled = existing.enabled;
      if (existing && entry.args) entry.args.push(...entryExtraArgs(existing, repoRoot, launch));
      if (existing && Array.isArray(entry.command)) entry.command.push(...entryExtraArgs(existing, repoRoot, launch));
      after = jsonDocument.upsertServerEntry(after ?? '{\n}\n', format, BACKENDS[backend].serverId, entry);
      if (before === null) after = `${JSON.stringify(jsonDocument.parseJsonDocument(after, filePath).root, null, 2)}\n`;
    }
    if (hadBom && after !== null) {
      after = `\uFEFF${after}`;
    }
  }

  if (after === before) {
    return { target, relative, file: filePath, agents: [...group.agents], ops: group.ops, before, after, action: 'unchanged', deleteFile: false };
  }
  if (!backend && before !== null) {
    const empty = format === 'codexToml' ? after.trim() === '' : jsonDocument.documentIsEmptySkeleton(after, format);
    // A whole-file delete additionally requires the ownership record: a
    // pre-existing empty object the user wrote is never "ours".
    if (empty && ownsFile(relative)) {
      return { target, relative, file: filePath, agents: [...group.agents], ops: group.ops, before, after, action: 'delete', deleteFile: true };
    }
  }
  return { target, relative, file: filePath, agents: [...group.agents], ops: group.ops, before, after, action: before === null ? 'create' : 'update', deleteFile: false };
}

function planLegacyCleanup({ repoRoot, format, configuring, relative, filePath, readFile, ownsFile, notes, migrate, launch }) {
  const before = readFile(filePath);
  if (before === null) {
    return null;
  }
  const document = jsonDocument.parseJsonDocument(before, filePath);
  const map = jsonDocument.walkServerMap(document.root, format, filePath);
  if (map === null) {
    return null;
  }
  let after = document.text;
  let touched = false;
  for (const id of configuredServerIds(map, repoRoot, launch)) {
    if (!Object.prototype.hasOwnProperty.call(map, id)) {
      continue;
    }
    if (!isOurServerEntry(map[id], repoRoot, launch)) {
      notes.push(`${relative}: 同名条目 ${id} 不是本集成的包装层（command/args 不指向本仓库），已保留不动`);
      continue;
    }
    if (configuring) migrate(id, map[id]);
    after = jsonDocument.removeServerEntry(after, format, id);
    touched = true;
  }
  if (!touched) {
    return null;
  }
  if (document.hadBom) after = `\uFEFF${after}`;
  if (jsonDocument.documentIsEmptySkeleton(after, format) && ownsFile(relative)) {
    return { relative, file: filePath, agents: [], ops: [], before, after, action: 'delete', deleteFile: true, legacy: true };
  }
  return { relative, file: filePath, agents: [], ops: [], before, after, action: 'update', deleteFile: false, legacy: true };
}
