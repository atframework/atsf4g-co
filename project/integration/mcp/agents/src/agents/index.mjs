/**
 * Aggregator over the per-agent configurator modules (one file per product).
 *
 * Builds the REGISTRY (product definitions) and TARGETS (physical-target
 * descriptors) surfaces consumed by configPlan/writers/setup; registry.mjs
 * re-exports them unchanged for compatibility. Shared-file groups declare
 * their target once in a shared module (mcp-json-group.mjs) and every member
 * references the same frozen object, so duplicate descriptors must be
 * byte-identical — a mismatch is a programming error and fails loudly here.
 */

import claude from './claude.mjs';
import pi from './pi.mjs';
import codebuddy from './codebuddy.mjs';
import codex from './codex.mjs';
import vscode from './vscode.mjs';
import cursor from './cursor.mjs';
import gemini from './gemini.mjs';
import opencode from './opencode.mjs';
import kilo from './kilo.mjs';
import roo from './roo.mjs';
import zcode from './zcode.mjs';
import omp from './omp.mjs';
import zed from './zed.mjs';
import kimiCode from './kimi-code.mjs';
import qwen from './qwen.mjs';
import workbuddy from './workbuddy.mjs';
import mimocode from './mimocode.mjs';
import cline from './cline.mjs';
import clineIde from './cline-ide.mjs';
import codebuddyIde from './codebuddy-ide.mjs';

export const CONFIGURATORS = Object.freeze([
  claude, pi, codebuddy, codex, vscode, cursor, gemini, opencode, kilo, roo,
  zcode, omp, zed, kimiCode, qwen, workbuddy, mimocode, cline, clineIde, codebuddyIde,
]);

export const REGISTRY = Object.freeze(CONFIGURATORS.map((configurator) => configurator.definition()));

export const GUIDED_IMPORTS = Object.freeze(Object.fromEntries(CONFIGURATORS
  .filter((configurator) => configurator.importSpec)
  .map((configurator) => [configurator.id, configurator.importSpec])));

/** Product-specific prerequisites and launch hints; setup only renders them. */
export function agentInstallNotes(agentId, context) {
  const configurator = CONFIGURATORS.find((entry) => entry.id === agentId);
  if (!configurator) throw new Error(`unknown agent ${agentId}`);
  return configurator.installNotes(context);
}

const targetDescriptors = new Map();
for (const configurator of CONFIGURATORS) {
  const descriptor = configurator.targetDescriptor();
  if (!descriptor) continue;
  const existing = targetDescriptors.get(descriptor.id);
  if (existing && JSON.stringify(existing) !== JSON.stringify(descriptor)) {
    throw new Error(`target ${descriptor.id} is declared differently by two configurators`);
  }
  targetDescriptors.set(descriptor.id, descriptor);
}

export const TARGETS = Object.freeze(Object.fromEntries(targetDescriptors));

export function agentDefinitions() {
  return REGISTRY;
}

export function agentById(agentId) {
  return CONFIGURATORS.find((configurator) => configurator.id === agentId || configurator.aliases.includes(agentId))?.definition() ?? null;
}

export function targetFor(agent) {
  return TARGETS[agent.targetId];
}

/** Normalize old string locations and locations that declare a different format. */
export function legacyLocations(target) {
  return (target.legacyFiles ?? []).map((entry) => typeof entry === 'string' ? { file: entry, format: 'mcpServers' } : entry);
}

/** Products with a physical repo target that this installer can write. */
export function autoConfigurableAgents() {
  return REGISTRY.filter((agent) => agent.targetId);
}

/** Guided-import products: delivery is guidance + build-dir snippet only. */
export function guidedAgents() {
  return REGISTRY.filter((agent) => !agent.targetId);
}

/** Products whose target resolves to the given targetId (shared-file group). */
export function productsForTarget(targetId) {
  return REGISTRY.filter((agent) => agent.targetId === targetId);
}
