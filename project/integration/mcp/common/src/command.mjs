import { spawnSync } from 'node:child_process';

/** Run an argument array without a shell; capture only bounded error excerpts. */
export function run(argv, { cwd = null, env = null, check = true, timeout = undefined } = {}) {
  const result = spawnSync(argv[0], argv.slice(1), { cwd, env, shell: false, encoding: 'utf8', windowsHide: true, timeout });
  if (check && result.status !== 0) {
    throw new Error(`command failed (${result.status}): ${argv.slice(0, 3).join(' ')}...\nstdout: ${String(result.stdout ?? '').slice(-2000)}\nstderr: ${String(result.stderr ?? '').slice(-2000)}`);
  }
  return result;
}
