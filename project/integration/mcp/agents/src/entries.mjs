/**
 * stdio server entry serialization per client format. Only fields verified
 * against each client's official docs are written; see
 * formats/codexToml.mjs for the TOML section variant.
 */

import path from 'node:path';

import { BACKENDS } from './backends.mjs';

/** A server id alone is not ownership evidence. Resolve only documented paths. */
export function isOurServerEntry(entry, repoRoot) {
  if (!entry || typeof entry !== 'object' || Array.isArray(entry)) return false;
  const command = Array.isArray(entry.command) ? entry.command[0] : entry.command;
  const argument = Array.isArray(entry.command) ? entry.command[1] : entry.args?.[0];
  if (command !== 'node' || typeof argument !== 'string') return false;
  const expanded = argument.replace(/^\$\{workspaceFolder\}(?=[\\/])/, () => repoRoot);
  if (!path.isAbsolute(expanded) && typeof entry.cwd !== 'string') return false;
  const base = typeof entry.cwd === 'string' ? path.resolve(repoRoot, entry.cwd) : repoRoot;
  return Object.values(BACKENDS).some((backend) => path.resolve(base, expanded) === path.resolve(repoRoot, backend.entry));
}

function absoluteServerEntry(repoRoot, backend) {
  return path.join(repoRoot, BACKENDS[backend].entry);
}

export function serverEntry(format, repoRoot, backend) {
  switch (format) {
    case 'vscodeServers':
      return {
        type: 'stdio',
        command: 'node',
        args: ['${workspaceFolder}/' + BACKENDS[backend].entry.split(path.sep).join('/')],
      };
    case 'mcpServersCwd':
      // Kimi Code / Qwen Code: officially supported per-server cwd (stdio only).
      return {
        command: 'node',
        args: [BACKENDS[backend].entry.split(path.sep).join('/')],
        cwd: repoRoot,
      };
    case 'zed':
      // Zed context_servers: flat command/args, no type field required.
      return {
        command: 'node',
        args: [absoluteServerEntry(repoRoot, backend)],
      };
    case 'opencode':
      // OpenCode / Kilo / MiMo: type local, command is [command, ...args].
      return {
        type: 'local',
        command: ['node', absoluteServerEntry(repoRoot, backend)],
        enabled: true,
      };
    // mcpServers (Claude/pi/CodeBuddy/Cursor/Gemini/Kilo-legacy/Roo/WorkBuddy/omp)
    // and zcode use the same stdio shape.
    default:
      return {
        type: 'stdio',
        command: 'node',
        args: [absoluteServerEntry(repoRoot, backend)],
      };
  }
}
