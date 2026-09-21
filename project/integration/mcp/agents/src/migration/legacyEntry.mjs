import path from 'node:path';
import { AgentConfigError } from '../errors.mjs';
import { serverEntry } from '../entries.mjs';

export function hasCustomOptions(entry, repoRoot) {
  return Object.keys(entry).some((key) => !['type', 'command', 'args', 'cwd', 'enabled'].includes(key))
    || (entry.cwd !== undefined && (typeof entry.cwd !== 'string' || path.resolve(repoRoot, entry.cwd) !== path.resolve(repoRoot)))
    || entry.enabled === false || (Array.isArray(entry.args) && entry.args.length > 1)
    || (Array.isArray(entry.command) && entry.command.length > 2);
}

/** Translate only verified stdio fields. Unknown cross-format options need a manual decision. */
export function migrateLegacyEntry({ entry, from, to, repoRoot, backend, relative }) {
  function refuse(reason) {
    throw new AgentConfigError(`${relative}: ${reason}; migrate these options manually (no files written)`, 'legacy-options-conflict');
  }
  const sameShape = from === to || ['mcpServers', 'mcpServersCwd'].includes(from) && ['mcpServers', 'mcpServersCwd'].includes(to);
  const base = serverEntry(to, repoRoot, backend);
  // An omitted default must not conflict with an explicit destination option.
  delete base.enabled;
  const extras = Array.isArray(entry.command) ? entry.command.slice(2) : (entry.args ?? []).slice(1);
  function hasFileReference(value) {
    if (typeof value === 'string') return /\{file:[^}]+\}/.test(value);
    return value !== null && typeof value === 'object' && Object.values(value).some(hasFileReference);
  }
  // Native clients resolve {file:...} relative to the configuration file;
  // old stdio clients can instead treat it literally. Do not guess its value.
  if (hasFileReference(entry)) refuse('file substitutions need origin-aware manual migration');
  if (entry.cwd !== undefined && (typeof entry.cwd !== 'string' || path.resolve(repoRoot, entry.cwd) !== path.resolve(repoRoot))) {
    refuse('custom cwd cannot be relocated safely');
  }
  if (sameShape) {
    const result = { ...entry, ...base };
    if (Object.hasOwn(entry, 'enabled')) result.enabled = entry.enabled;
    if (Array.isArray(base.command)) result.command.push(...extras);
    else result.args.push(...extras);
    return result;
  }
  if (from !== 'mcpServers' || to !== 'opencode') refuse(`unsupported conversion ${from} -> ${to}`);
  const supported = ['type', 'command', 'args', 'cwd', 'env', 'disabled'];
  if (Object.keys(entry).some((key) => !supported.includes(key))) refuse('unknown cross-format fields');
  if (entry.type !== undefined && entry.type !== 'stdio') refuse('unsupported transport type');
  if (entry.disabled !== undefined && typeof entry.disabled !== 'boolean') refuse('disabled must be boolean');
  if (entry.env !== undefined && (!entry.env || typeof entry.env !== 'object' || Array.isArray(entry.env)
    || Object.values(entry.env).some((value) => typeof value !== 'string'))) refuse('env must contain strings');
  base.command.push(...extras);
  if (entry.env !== undefined) base.environment = entry.env;
  if (entry.disabled !== undefined) base.enabled = !entry.disabled;
  return base;
}
