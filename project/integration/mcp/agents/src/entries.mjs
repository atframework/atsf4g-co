/**
 * stdio server entry serialization per client format. Only fields verified
 * against each client's official docs are written; see
 * formats/codexToml.mjs for the TOML section variant.
 */

import path from 'node:path';

import { BACKENDS, managedServerIds, backendForServerId } from './backends.mjs';
import { samePath, isWithin } from '../../common/src/paths.mjs';
import { AgentConfigError } from './errors.mjs';

/** Scope arguments are owned by the installer; optional backend arguments survive. */
export function entryExtraArgs(entry, repoRoot, launch = {}) {
  const args = Array.isArray(entry.command) ? entry.command.slice(2) : (entry.args ?? []).slice(1);
  const extras = [];
  const seen = new Set();
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    const flag = typeof arg === 'string' ? arg.split('=')[0] : '';
    if (!['--repo-root', '--build-dir'].includes(flag)) { extras.push(arg); continue; }
    const value = arg.includes('=') ? arg.slice(arg.indexOf('=') + 1) : args[++i];
    const expected = flag === '--repo-root' ? repoRoot : launch.buildDir;
    if (seen.has(flag) || typeof value !== 'string' || !value || (flag === '--repo-root' && !path.isAbsolute(value)) || (expected && !samePath(path.resolve(repoRoot, value), path.resolve(expected)))) {
      throw new AgentConfigError(`${flag}: existing wrapper scope differs from this workspace`, 'server-conflict');
    }
    seen.add(flag);
    // Without launch context, retain a user's explicit build location.
    if (flag === '--build-dir' && !expected) extras.push(flag, value);
  }
  return extras;
}

/** A server id alone is not ownership evidence. Resolve only documented paths. */
export function isOurServerEntry(entry, repoRoot, launch = {}, expectedBackend = null) {
  if (!entry || typeof entry !== 'object' || Array.isArray(entry)) return false;
  const command = Array.isArray(entry.command) ? entry.command[0] : entry.command;
  const argument = Array.isArray(entry.command) ? entry.command[1] : entry.args?.[0];
  if (command !== 'node' || typeof argument !== 'string') return false;
  const expanded = argument.replace(/^\$\{workspaceFolder\}(?=[\\/])/, () => repoRoot);
  if (!path.isAbsolute(expanded) && typeof entry.cwd !== 'string') return false;
  const base = typeof entry.cwd === 'string' ? path.resolve(repoRoot, entry.cwd) : repoRoot;
  try { entryExtraArgs(entry, repoRoot, launch); } catch { return false; }
  return Object.keys(BACKENDS).some((backend) => {
    if (expectedBackend && backend !== expectedBackend) return false;
    const expected = path.resolve(absoluteServerEntry(repoRoot, backend, launch));
    if (!samePath(path.resolve(base, expanded), expected)) return false;
    if (isWithin(expected, repoRoot)) return true;
    // An external script without an explicit root would fall back to the
    // toolkit's own workspace. Its path alone is not ownership evidence.
    const args = Array.isArray(entry.command) ? entry.command.slice(2) : (entry.args ?? []).slice(1);
    return args.some((arg) => typeof arg === 'string' && (arg === '--repo-root' || arg.startsWith('--repo-root=')));
  });
}

function absoluteServerEntry(repoRoot, backend, launch = {}) {
  return launch.integrationRoot ? path.join(launch.integrationRoot, backend, 'src', 'server.mjs') : BACKENDS[backend].entry;
}

export function configuredServerIds(map, repoRoot, launch = {}) {
  return [...new Set([...managedServerIds(), ...Object.keys(map ?? {}).filter(id => backendForServerId(id) && isOurServerEntry(map[id], repoRoot, launch, backendForServerId(id)))])];
}

export function serverEntry(format, repoRoot, backend, launch = {}) {
  const absolute = absoluteServerEntry(repoRoot, backend, launch);
  const local = isWithin(absolute, repoRoot);
  const scope = ['--repo-root', repoRoot];
  if (launch.buildDir) scope.push('--build-dir', launch.buildDir);
  const relative = path.relative(repoRoot, absolute).split(path.sep).join('/');
  switch (format) {
    case 'vscodeServers':
      return {
        type: 'stdio',
        command: 'node',
        args: [local ? '${workspaceFolder}/' + relative : absolute, ...scope],
      };
    case 'mcpServersCwd':
      // Kimi Code / Qwen Code: officially supported per-server cwd (stdio only).
      return {
        command: 'node',
        args: [local ? relative : absolute, ...scope],
        cwd: repoRoot,
      };
    case 'zed':
      // Zed context_servers: flat command/args, no type field required.
      return {
        command: 'node',
        args: [absolute, ...scope],
      };
    case 'opencode':
      // OpenCode / Kilo / MiMo: type local, command is [command, ...args].
      return {
        type: 'local',
        command: ['node', absolute, ...scope],
        enabled: true,
      };
    // mcpServers (Claude/pi/CodeBuddy/Cursor/Gemini/Kilo-legacy/Roo/WorkBuddy/omp)
    // and zcode use the same stdio shape.
    default:
      return {
        type: 'stdio',
        command: 'node',
        args: [absolute, ...scope],
      };
  }
}
