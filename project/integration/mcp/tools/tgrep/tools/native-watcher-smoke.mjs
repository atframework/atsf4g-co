/** Real-backend regression; pass a patched binary and workspace-local scratch directory. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { WorkspacePaths } from '../../../common/src/paths.mjs';
import { TgrepBackend } from '../src/backend.mjs';

if (process.argv.length !== 4) throw new Error('usage: node native-watcher-smoke.mjs <tgrep-binary> <workspace-scratch-dir>');
const binary = path.resolve(process.argv[2]);
const scratch = path.resolve(process.argv[3]);
fs.mkdirSync(scratch, { recursive: true });
const fixture = fs.mkdtempSync(path.join(scratch, 'native-watcher-'));
// Starting inside an ignored ancestor is valid. Initial walking and native
// event filtering must agree, while rules for files inside the root still apply.
fs.mkdirSync(path.join(fixture, '.git'));
fs.writeFileSync(path.join(fixture, '.gitignore'), '/ignored-root/\n*.ignored.cpp\n');
const root = path.join(fixture, 'ignored-root/project');
fs.mkdirSync(path.join(root, 'Source'), { recursive: true });
fs.writeFileSync(path.join(root, 'Game.uproject'), '{}');
const paths = new WorkspacePaths(root, path.join(root, 'build'));
fs.mkdirSync(paths.agentTmpDir, { recursive: true });
const cached = path.join(paths.agentTmpDir, 'Probe.cpp');
fs.writeFileSync(cached, 'int NativeCacheExcluded() { return 1; }\n');
const file = path.join(root, 'Source/Probe.cpp');
const ignored = path.join(root, 'Source/Probe.ignored.cpp');
fs.writeFileSync(file, 'int NativeOriginal() { return 11; }\n');
fs.writeFileSync(ignored, 'int NativeIgnored() { return 12; }\n');
const measurements = [];

async function until(label, predicate) {
  const started = Date.now();
  // Far below the backend's default poll interval; success must come from events.
  while (Date.now() - started < 10_000) {
    if (await predicate()) { measurements.push({ label, elapsed_ms: Date.now() - started }); return; }
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  throw new Error(`${label} did not converge; inspect ${fixture}`);
}

function backend() { return new TgrepBackend({ paths, binary, stderrLog: path.join(fixture, 'backend.log') }); }
async function ready(service) {
  await service.start();
  await until('ready', async () => { const s = await service.status(); return !s.indexing && s.hidden_complete; });
  const status = await service.status();
  assert.equal(status.watch_mode_active, 'native');
  assert.equal(status.watcher_active, true);
  assert.ok(status.poll_interval_secs >= 120);
}
async function matches(service, pattern) { return (await service.search({ pattern, fixed_string: true })).matches; }
let service = backend();
try {
  await ready(service);
  assert.equal((await matches(service, 'NativeOriginal')).length, 1);
  assert.equal((await matches(service, 'NativeCacheExcluded')).length, 0);
  fs.writeFileSync(cached, 'int NativeCacheExcludedChanged() { return 2; }\n');
  fs.writeFileSync(file, 'int NativeChanged() { return 17; }\n');
  await until('edit', async () => (await matches(service, 'NativeChanged')).length === 1);
  assert.equal((await matches(service, 'NativeOriginal')).length, 0);
  assert.equal((await matches(service, 'NativeIgnored')).length, 0);
  fs.writeFileSync(ignored, 'int NativeIgnoredAgain() { return 19; }\n');

  const added = path.join(root, 'Source/Added.cpp');
  const renamed = path.join(root, 'Source/Renamed.cpp');
  fs.writeFileSync(added, 'int NativeAdded() { return 23; }\n');
  await until('add', async () => (await matches(service, 'NativeAdded')).length === 1);
  fs.renameSync(added, renamed);
  await until('rename', async () => {
    const rows = await matches(service, 'NativeAdded');
    const files = (await service.files({})).files;
    return rows.length === 1 && rows[0].file === 'Source/Renamed.cpp'
      && files.includes('Source/Renamed.cpp') && !files.includes('Source/Added.cpp');
  });
  fs.unlinkSync(renamed);
  await until('delete', async () => (await matches(service, 'NativeAdded')).length === 0
    && !(await service.files({})).files.includes('Source/Renamed.cpp'));
  assert.equal((await matches(service, 'NativeIgnoredAgain')).length, 0);
  assert.equal((await matches(service, 'NativeCacheExcludedChanged')).length, 0);
  assert.equal((await service.status()).last_reconcile_at, null, 'online updates must not depend on periodic reconcile');
  await service.stop();
  assert.equal(service.alive(), false);

  fs.writeFileSync(file, 'int NativeOffline() { return 29; }\n');
  service = backend();
  await ready(service);
  await until('reconnect', async () => (await matches(service, 'NativeOffline')).length === 1);
  assert.equal((await matches(service, 'NativeChanged')).length, 0);
  fs.writeFileSync(file, 'int NativeWarmEdit() { return 31; }\n');
  await until('warm edit', async () => (await matches(service, 'NativeWarmEdit')).length === 1);
  console.log(JSON.stringify({ binary, fixture, measurements }));
} finally {
  await service.stop();
  assert.equal(service.alive(), false);
}
