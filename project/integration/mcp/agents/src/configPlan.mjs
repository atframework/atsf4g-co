/**
 * Pure change planning for agent config files (no I/O of its own).
 *
 * planConfigChanges receives injectable `readFile` / `ownsFile` accessors and
 * groups operations by TARGET (several products may share one physical file).
 * Per target it resolves the effective candidate file (exclusive candidates
 * conflict when several exist; priority candidates use the first existing
 * one), computes the primary edit, and appends legacy-cleanup steps that run
 * only after the primary write succeeded (migration rules in Plan.md 11.3):
 *
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
import { serverEntry, isOurServerEntry } from './entries.mjs';
import { REGISTRY, TARGETS, managedServerIds, BACKENDS, legacyLocations } from './registry.mjs';
import * as jsonDocument from './formats/jsonDocument.mjs';
import * as codexToml from './formats/codexToml.mjs';

export function planConfigChanges({ repoRoot, operations, readFile, ownsFile = () => false, registry = REGISTRY, targets = TARGETS }) {
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
      const planned = planTarget({ repoRoot, group, readFile, ownsFile });
      steps.push(...planned.steps);
      notes.push(...planned.notes);
    } catch (error) {
      problems.push({ targetId: group.targetId, agents: [...group.agents], error });
    }
  }
  return { steps, problems, notes };
}

/** Resolve the effective candidate file for a target (see module doc). Returns the file, its current text (null = missing), and legacy files needing cleanup. */
function resolveTargetFile({ repoRoot, target, group, readFile }) {
  const candidates = target.candidates ?? [target.file];
  const contents = new Map(candidates.map((candidate) => [candidate, readFile(path.join(repoRoot, candidate))]));
  const existing = candidates.filter((candidate) => contents.get(candidate) !== null);

  if ((target.candidatesMode ?? 'single') === 'exclusive' && existing.length > 1) {
    throw new AgentConfigError(
      `multiple config candidates exist (${existing.map((candidate) => candidate.split(path.sep).join('/')).join(', ')}); decide which one is effective and remove the others, or edit manually — no file was written`,
      'candidate-conflict',
    );
  }

  if (target.candidatesMode === 'priority' && target.legacyFallback && existing.length === 1 && existing[0] === target.legacyFallback.candidate) {
    const selected = group.ops.filter((op) => op.type === 'configure').map((op) => op.agent.id);
    const soleLegacyOwner = selected.length > 0 && selected.every((id) => target.legacyFallback.inPlaceOwnerIds.includes(id));
    const legacyRoot = jsonDocument.parseJsonDocument(contents.get(existing[0]), existing[0]).root;
    const map = jsonDocument.walkServerMap(legacyRoot, target.format, existing[0]) ?? {};
    const hasForeign = Object.keys(map).some((id) => !managedServerIds().includes(id) || !isOurServerEntry(map[id], repoRoot));
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

function planTarget({ repoRoot, group, readFile, ownsFile }) {
  const target = { ...group.target, targetId: group.targetId };
  const { file: effective, before: primaryBefore, legacyCleanups } = resolveTargetFile({ repoRoot, target, group, readFile });
  const primary = planFileEdit({ repoRoot, relative: effective.split(path.sep).join('/'), filePath: path.join(repoRoot, effective), target, group, before: primaryBefore, ownsFile });

  const steps = [primary];
  const notes = [];
  for (const { file: legacyRelative, format } of [...legacyLocations(target), ...legacyCleanups.map((file) => ({ file, format: target.format }))]) {
    const step = planLegacyCleanup({ repoRoot, format, configuring: group.ops.some((op) => op.type === 'configure'), relative: legacyRelative.split(path.sep).join('/'), filePath: path.join(repoRoot, legacyRelative), readFile, ownsFile, notes });
    if (step) {
      steps.push(step);
    }
  }
  return { steps, notes };
}

function planFileEdit({ repoRoot, relative, filePath, target, group, before, ownsFile }) {
  const format = target.format;
  const backends = new Set(group.ops.filter((op) => op.type === 'configure').map((op) => op.backend));
  if (backends.size > 1) {
    throw new AgentConfigError(`${relative}: conflicting backends (${[...backends].join(', ')}) requested for one file`, 'conflict');
  }
  const backend = backends.size === 1 ? [...backends][0] : null;

  let after;
  if (format === 'codexToml') {
    if (before !== null) codexToml.assertCodexServers(before, repoRoot);
    after = backend ? codexToml.configureCodexToml(before, repoRoot, backend)
      : before === null ? null : codexToml.removeCodexTomlServers(before).text;
  } else {
    let hadBom = false;
    if (before === null) {
      after = null;
    } else {
      const document = jsonDocument.parseJsonDocument(before, filePath);
      const map = jsonDocument.walkServerMap(document.root, format, filePath);
      after = document.text;
      hadBom = document.hadBom;
      // Keep the selected entry in place; field edits preserve user options/comments.
      for (const id of managedServerIds()) {
        if (map && Object.hasOwn(map, id) && !isOurServerEntry(map[id], repoRoot)) {
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
      const entry = serverEntry(format, repoRoot, backend);
      const existing = after && jsonDocument.walkServerMap(jsonDocument.parseJsonDocument(after, filePath).root, format, filePath)?.[BACKENDS[backend].serverId];
      if (existing && Object.hasOwn(existing, 'enabled')) entry.enabled = existing.enabled;
      if (Array.isArray(existing?.args) && entry.args) entry.args.push(...existing.args.slice(1));
      if (Array.isArray(existing?.command) && Array.isArray(entry.command)) entry.command.push(...existing.command.slice(2));
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
    // pre-existing empty object the user wrote is never "ours" (Plan.md 11.4).
    if (empty && ownsFile(relative)) {
      return { target, relative, file: filePath, agents: [...group.agents], ops: group.ops, before, after, action: 'delete', deleteFile: true };
    }
  }
  return { target, relative, file: filePath, agents: [...group.agents], ops: group.ops, before, after, action: before === null ? 'create' : 'update', deleteFile: false };
}

function planLegacyCleanup({ repoRoot, format, configuring, relative, filePath, readFile, ownsFile, notes }) {
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
  for (const id of managedServerIds()) {
    if (!Object.prototype.hasOwnProperty.call(map, id)) {
      continue;
    }
    if (!isOurServerEntry(map[id], repoRoot)) {
      notes.push(`${relative}: 同名条目 ${id} 不是本集成的包装层（command/args 不指向本仓库），已保留不动`);
      continue;
    }
    const entry = map[id];
    const custom = Object.keys(entry).some((key) => !['type', 'command', 'args', 'cwd', 'enabled'].includes(key))
      || entry.enabled === false || (Array.isArray(entry.args) && entry.args.length > 1)
      || (Array.isArray(entry.command) && entry.command.length > 2);
    if (configuring && custom) {
      throw new AgentConfigError(`${relative}: ${id} has custom settings; migrate those settings to the new target before retrying (no files written)`, 'legacy-options-conflict');
    }
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
