/** Runtime selection for the installer and the stdio wrappers, not upstream binaries. */
import path from 'node:path';
import process from 'node:process';

export const BUN_SCRIPT_ARGS = Object.freeze(['--no-install', '--no-env-file']);

// MCP starts without a terminal. Use only prepared node_modules, no project
// import maps/lockfiles or runtime downloads, and never prompt for permissions.
export const DENO_SCRIPT_ARGS = Object.freeze([
  'run', '--allow-read', '--allow-write', '--allow-env', '--allow-run', '--allow-sys',
  '--no-prompt', '--no-config', '--no-lock', '--node-modules-dir=manual', '--cached-only',
]);

export function currentRuntime({ versions = process.versions, executable = process.execPath } = {}) {
  // Bun and Deno also expose versions.node; check their own identity first.
  const kind = versions.deno ? 'deno' : versions.bun ? 'bun' : versions.node ? 'node' : null;
  if (!kind) throw new Error('MCP supports Node.js, Bun and Deno runtimes');
  return { kind, executable, version: versions[kind] };
}

export function runtimeScriptArgs(runtime = currentRuntime()) {
  if (!['node', 'bun', 'deno'].includes(runtime.kind) || !path.isAbsolute(runtime.executable)) {
    throw new Error('MCP runtime must be Node.js, Bun or Deno with an absolute executable path');
  }
  return runtime.kind === 'deno' ? [...DENO_SCRIPT_ARGS] : runtime.kind === 'bun' ? [...BUN_SCRIPT_ARGS] : [];
}

export function scriptInvocation(script, args = [], runtime = currentRuntime()) {
  return { command: runtime.executable, args: [...runtimeScriptArgs(runtime), script, ...args] };
}

/** Recognize only a direct runtime invocation that this installer can manage. */
export function parseScriptInvocation(command, args) {
  if (typeof command !== 'string' || !Array.isArray(args) || args.some(arg => typeof arg !== 'string')) return null;
  const name = path.basename(command).toLowerCase().replace(/\.exe$/, '');
  if (!['node', 'bun', 'deno'].includes(name)) return null;
  if (command !== name && command !== name + '.exe' && !path.isAbsolute(command)) return null;
  const prefix = name === 'deno' ? DENO_SCRIPT_ARGS : name === 'bun' && args[0] === BUN_SCRIPT_ARGS[0] ? BUN_SCRIPT_ARGS : [];
  if (prefix.some((value, i) => args[i] !== value)) return null;
  const script = args[prefix.length];
  if (!script || script.startsWith('-')) return null;
  return { kind: name, script, args: args.slice(prefix.length + 1) };
}

export function runtimeSupported(runtime = currentRuntime()) {
  const major = Number(runtime.version?.split('.')[0]);
  return runtime.kind === 'node' ? major >= 20 : runtime.kind === 'deno' ? major >= 2 : runtime.kind === 'bun' && major >= 1;
}
