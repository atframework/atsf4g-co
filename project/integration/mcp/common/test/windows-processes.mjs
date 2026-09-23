import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';

/** Console hosts can retain cwd briefly after their backend's process exits. */
export function windowsDescendants(pid) {
  if (process.platform !== 'win32') return [];
  const result = spawnSync('pwsh.exe', ['-NoLogo', '-NoProfile', '-NonInteractive', '-Command',
    'Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId | ConvertTo-Json -Compress'],
  { encoding: 'utf8', windowsHide: true, timeout: 10000 });
  assert.equal(result.status, 0, result.stderr);
  const processes = JSON.parse(result.stdout);
  const descendants = [];
  let parents = [pid];
  while (parents.length) {
    parents = processes.filter(row => parents.includes(row.ParentProcessId)).map(row => row.ProcessId);
    descendants.push(...parents);
  }
  return descendants;
}
