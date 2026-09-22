/**
 * Configurator base classes for the per-agent modules in this directory.
 *
 * Each product (agent CLI/IDE) gets its own module exporting one configurator
 * instance; everything product-specific — metadata, physical target, candidate
 * and legacy layout, evidence links — lives there. Config styles shared by
 * several products are expressed as base classes so the shared knowledge
 * exists once:
 *
 * - `AgentConfigurator`: metadata + declarative physical-target descriptor.
 * - `JsonServerMapConfigurator`: one JSON/JSONC document holding a server map
 *   (the `mcpServers` family and its per-client variants).
 * - `OpenCodeShapeConfigurator`: the OpenCode shape (`mcp` map, command array,
 *   same-directory candidates that consolidate when several exist).
 * - `CodexTomlConfigurator`: the Codex `[mcp_servers.*]` TOML section.
 * - `GuidedImportConfigurator`: IDE products without a verified project file;
 *   delivery is guidance plus a build-dir snippet, never a repo config file.
 *
 * The planning engine (configPlan/formats/fileStore) stays product-agnostic:
 * it consumes the declarative descriptors, so adding an agent usually means
 * adding one module and nothing else.
 */

import path from 'node:path';

/** File declarations accept 'a/b.json' or ['a', 'b.json']; normalize to platform form. */
function toPlatformFile(file) {
  return Array.isArray(file) ? path.join(...file) : file;
}

function normalizeTarget(target) {
  const descriptor = {
    id: target.id,
    file: toPlatformFile(target.file),
    format: target.format,
    pathPolicy: target.pathPolicy,
  };
  if (target.candidates) descriptor.candidates = target.candidates.map(toPlatformFile);
  if (target.candidatesMode) descriptor.candidatesMode = target.candidatesMode;
  if (target.legacyFallback) descriptor.legacyFallback = target.legacyFallback;
  if (target.legacyDirectory) descriptor.legacyDirectory = target.legacyDirectory;
  if (target.legacyFileSuffix) descriptor.legacyFileSuffix = target.legacyFileSuffix;
  if (target.copyLegacyRemainder) descriptor.copyLegacyRemainder = true;
  if (target.legacyFiles) {
    descriptor.legacyFiles = target.legacyFiles.map((entry) => (typeof entry === 'string' || Array.isArray(entry)
      ? toPlatformFile(entry)
      : { ...entry, file: toPlatformFile(entry.file) }));
  }
  if (target.evidence) descriptor.evidence = target.evidence;
  if (target.entryDefaults) descriptor.entryDefaults = Object.freeze({ ...target.entryDefaults });
  return Object.freeze(descriptor);
}

export class AgentConfigurator {
  /**
   * @param {object} options
   * @param {string} options.id product id used by `--agents`
   * @param {string} options.label human label shown in menus
   * @param {string[]} [options.aliases] older ids that resolve to this product
   * @param {'auto'|'extension'|'export'|'guided'} [options.capability]
   * @param {string} [options.prerequisites] user-facing prerequisite note
   * @param {object|null} [options.target] physical-target declaration (null = guided import)
   */
  constructor({ id, label, aliases = [], capability = 'auto', prerequisites = null, target = null, installNotes = () => ({ guidance: [], pending: [] }) }) {
    this.id = id;
    this.label = label;
    this.aliases = [...aliases];
    this.capability = capability;
    this.prerequisites = prerequisites;
    this.target = target;
    this.installNotes = installNotes;
  }

  get targetId() {
    return this.target ? this.target.id : null;
  }

  /** Product definition consumed by menus, planning, and the registry surface. */
  definition() {
    const definition = { id: this.id, label: this.label, aliases: [...this.aliases], capability: this.capability };
    if (this.prerequisites) definition.prerequisites = this.prerequisites;
    definition.targetId = this.targetId;
    return Object.freeze(definition);
  }

  /** Normalized physical-target descriptor for configPlan; null for guided imports. */
  targetDescriptor() {
    return this.target ? normalizeTarget(this.target) : null;
  }
}

/** One JSON/JSONC document holding a server map (`mcpServers` family and variants). */
export class JsonServerMapConfigurator extends AgentConfigurator {
  constructor({ format = 'mcpServers', pathPolicy = 'absolute-args', target = null, ...rest }) {
    super({ ...rest, target: target ? { ...target, format, pathPolicy } : null });
  }
}

/**
 * OpenCode-shape agents (OpenCode / Kilo / MiMo): `mcp` map with
 * `command: [program, ...args]` entries; json/jsonc candidates consolidate
 * within the selected directory, keeping other configuration layers intact.
 */
export class OpenCodeShapeConfigurator extends JsonServerMapConfigurator {
  constructor({ target = null, ...rest }) {
    super({ ...rest, format: 'opencode', pathPolicy: 'absolute-args', target: target ? { candidatesMode: 'exclusive', ...target } : null });
  }
}

/** Codex: CLI and IDE share managed `[mcp_servers.<id>]` tables in `.codex/config.toml`. */
export class CodexTomlConfigurator extends AgentConfigurator {
  constructor({ target = null, ...rest }) {
    super({ ...rest, target: target ? { format: 'codexToml', pathPolicy: 'cwd-relative', ...target } : null });
  }
}

/** IDE products without a verified project-level file: guidance + build-dir snippet only. */
export class GuidedImportConfigurator extends AgentConfigurator {
  constructor({ capability = 'guided', importSpec, ...rest }) {
    super({ ...rest, capability, target: null });
    this.importSpec = importSpec;
  }
}
