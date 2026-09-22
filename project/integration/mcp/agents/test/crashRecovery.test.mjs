import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';
import { once } from 'node:events';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { createFileStore, findOpenJournalBatch, recoverInterruptedBatch } from '../src/fileStore.mjs';
import { applyAgentConfigChanges, configureAgent, planAgentConfigChanges, removeAgentServers, runAgentConfigBatch } from './fixtures.mjs';

const digest = (bytes) => crypto.createHash('sha256').update(bytes).digest('hex');

function workspace(t) {
  const repoRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-crash-'));
  t.after(() => fs.rmSync(repoRoot, { recursive: true, force: true }));
  const options = { repoRoot, stateDir: path.join(repoRoot, 'state'), tmpDir: path.join(repoRoot, 'tmp') };
  return { ...options };
}

/** A pid that has definitely exited (spawnSync waits for termination). */
function deadPid() {
  return spawnSync(process.execPath, ['-e', '']).pid;
}

/** Rewrite the recorded batch owner so liveness checks see a dead process. */
function pretendOwnerDied({ tmpDir, from = process.pid, to = deadPid() }) {
  const journalPath = path.join(tmpDir, 'agent-config-journal.jsonl');
  const text = fs.readFileSync(journalPath, 'utf8').replaceAll(`"pid":${from}`, `"pid":${to}`);
  fs.writeFileSync(journalPath, text);
  return to;
}

const recover = (w, ownerIsDead) => recoverInterruptedBatch({ ...w, ownerIsDead });

function journalFile(w) { return path.join(w.tmpDir, 'agent-config-journal.jsonl'); }

test('review: a live writer keeps even a torn journal tail byte-for-byte', (t) => {
  const w = workspace(t);
  const store = createFileStore(w);
  store.write(path.join(w.repoRoot, 'cfg.json'), 'after', { relative: 'cfg.json', expectedBefore: null });
  fs.appendFileSync(journalFile(w), '{"action":"planned"');
  const before = fs.readFileSync(journalFile(w));
  assert.throws(() => recover(w), /still running/);
  assert.deepEqual(fs.readFileSync(journalFile(w)), before);
});

test('review: a malformed complete last line is corruption, not a torn append', (t) => {
  const w = workspace(t);
  fs.mkdirSync(w.tmpDir, { recursive: true });
  fs.writeFileSync(journalFile(w), '{bad json}\n');
  assert.throws(() => recover(w), (error) => error.kind === 'journal-corrupted');
  assert.equal(fs.readFileSync(journalFile(w), 'utf8'), '{bad json}\n');
});

test('review: semantically damaged plans abort the entire recovery before any file changes', (t) => {
  const w = workspace(t);
  const store = createFileStore(w);
  const file = path.join(w.repoRoot, 'cfg.json');
  store.write(file, 'after', { relative: 'cfg.json', expectedBefore: null });
  pretendOwnerDied(w);
  const lines = fs.readFileSync(journalFile(w), 'utf8').trimEnd().split('\n').map(JSON.parse);
  lines.push({ action: 'planned', batch: lines[0].batch, file: 'other.json', operation: 'update', after_digest: digest('other') });
  fs.writeFileSync(journalFile(w), lines.map(JSON.stringify).join('\n') + '\n');
  const before = fs.readFileSync(journalFile(w));
  assert.throws(() => recover(w), (error) => error.kind === 'journal-corrupted');
  assert.equal(fs.readFileSync(file, 'utf8'), 'after');
  assert.deepEqual(fs.readFileSync(journalFile(w)), before);
});

test('review: create then update in a crashed batch restores absence', (t) => {
  const w = workspace(t);
  const store = createFileStore(w);
  const file = path.join(w.repoRoot, 'cfg.json');
  store.write(file, 'first', { relative: 'cfg.json', expectedBefore: null });
  store.write(file, 'second', { relative: 'cfg.json', expectedBefore: 'first' });
  pretendOwnerDied(w);
  assert.deepEqual(recover(w).removed, ['cfg.json']);
  assert.equal(fs.existsSync(file), false);
});

test('review: an interrupted second write still restores the first write to pre-batch bytes', (t) => {
  const w = workspace(t);
  const store = createFileStore(w);
  const file = path.join(w.repoRoot, 'cfg.json');
  fs.writeFileSync(file, 'original');
  store.write(file, 'first', { relative: 'cfg.json', expectedBefore: 'original' });
  const rename = fs.renameSync;
  const injection = t.mock.method(fs, 'renameSync', (source, destination) => {
    if (destination === file) throw new Error('second rename interrupted');
    return rename(source, destination);
  });
  assert.throws(() => store.write(file, 'second', { relative: 'cfg.json', expectedBefore: 'first' }), /second rename interrupted/);
  injection.mock.restore();
  pretendOwnerDied(w);
  assert.deepEqual(recover(w).restored, ['cfg.json']);
  assert.equal(fs.readFileSync(file, 'utf8'), 'original');
});

test('review: recovered installer-owned files remain owned and uninstall removes the skeleton', (t) => {
  const w = workspace(t);
  configureAgent({ ...w, agentId: 'cursor', backend: 'tgrep' });
  const file = path.join(w.repoRoot, '.cursor/mcp.json');
  const before = fs.readFileSync(file, 'utf8');
  const store = createFileStore(w);
  store.write(file, before + '\n', { relative: '.cursor/mcp.json', expectedBefore: before });
  pretendOwnerDied(w);
  recover(w);
  assert.equal(createFileStore(w).owns('.cursor/mcp.json'), true);
  assert.equal(removeAgentServers({ ...w, agentId: 'cursor' }).action, 'removed-file');
  assert.equal(fs.existsSync(file), false);
});

test('review: failed batch-end append rolls back and closes the same batch', (t) => {
  const w = workspace(t);
  const append = fs.appendFileSync;
  t.mock.method(fs, 'appendFileSync', (file, text, options) => {
    if (String(text).includes('"status":"committed"')) throw new Error('commit journal failed');
    return append(file, text, options);
  });
  const plan = planAgentConfigChanges({ ...w, operations: [{ type: 'configure', agentId: 'cursor', backend: 'tgrep' }] });
  assert.throws(() => applyAgentConfigChanges({ ...w, plan }), /commit journal failed/);
  assert.equal(fs.existsSync(path.join(w.repoRoot, '.cursor/mcp.json')), false);
  assert.equal(findOpenJournalBatch(w), null);
});

test('review: an overlapping store cannot start another journal batch', (t) => {
  const w = workspace(t);
  const first = createFileStore(w);
  first.write(path.join(w.repoRoot, 'one.json'), 'one', { relative: 'one.json', expectedBefore: null });
  const before = fs.readFileSync(journalFile(w));
  const second = createFileStore(w);
  assert.throws(() => second.write(path.join(w.repoRoot, 'two.json'), 'two', { relative: 'two.json', expectedBefore: null }), /still running/);
  assert.equal(fs.existsSync(path.join(w.repoRoot, 'two.json')), false);
  assert.deepEqual(fs.readFileSync(journalFile(w)), before);
  first.finishBatch('committed');
});

test('review: writers exclude contenders before batch-begin is appended', (t) => {
  const w = workspace(t);
  const first = createFileStore(w);
  const second = createFileStore(w);
  const append = fs.appendFileSync;
  let attempted = false;
  t.mock.method(fs, 'appendFileSync', (file, text, options) => {
    if (!attempted && String(text).includes('"action":"batch-begin"')) {
      attempted = true;
      assert.throws(() => second.write(path.join(w.repoRoot, 'two.json'), 'two', { relative: 'two.json', expectedBefore: null }), /still running/);
    }
    return append(file, text, options);
  });
  first.write(path.join(w.repoRoot, 'one.json'), 'one', { relative: 'one.json', expectedBefore: null });
  first.finishBatch('committed');
  assert.equal(attempted, true);
  assert.equal(fs.existsSync(path.join(w.repoRoot, 'two.json')), false);
  assert.equal(findOpenJournalBatch(w), null);
});

test('review: only one recovery can replace files from an interrupted batch', (t) => {
  const w = workspace(t);
  const store = createFileStore(w);
  const file = path.join(w.repoRoot, 'cfg.json');
  fs.writeFileSync(file, 'before');
  store.write(file, 'after', { relative: 'cfg.json', expectedBefore: 'before' });
  pretendOwnerDied(w);
  const rename = fs.renameSync;
  let attempted = false;
  t.mock.method(fs, 'renameSync', (source, destination) => {
    if (destination === file) {
      attempted = true;
      assert.throws(() => recover(w), /still running/);
    }
    return rename(source, destination);
  });
  assert.deepEqual(recover(w).restored, ['cfg.json']);
  assert.equal(attempted, true);
  assert.equal(fs.readFileSync(file, 'utf8'), 'before');
});

test('review: a killed writer after replacement restores ownership and releases its claim', async (t) => {
  const w = workspace(t);
  configureAgent({ ...w, agentId: 'cursor', backend: 'tgrep' });
  const file = path.join(w.repoRoot, '.cursor/mcp.json');
  const before = fs.readFileSync(file, 'utf8');
  const child = spawn(process.execPath, ['--input-type=module', '-e', `
    import fs from 'node:fs';
    import { createFileStore } from ${JSON.stringify(new URL('../src/fileStore.mjs', import.meta.url).href)};
    const w = JSON.parse(process.argv[1]);
    const file = process.argv[2];
    const before = fs.readFileSync(file, 'utf8');
    const rename = fs.renameSync;
    fs.renameSync = (source, destination) => {
      rename(source, destination);
      if (destination === file) {
        process.send('replaced');
        Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0);
      }
    };
    createFileStore(w).write(file, before + '\\n', { relative: '.cursor/mcp.json', expectedBefore: before });
  `, JSON.stringify(w), file], { stdio: ['ignore', 'ignore', 'pipe', 'ipc'], windowsHide: true });
  t.after(() => { if (child.exitCode === null && child.signalCode === null) child.kill('SIGKILL'); });
  const timeout = setTimeout(() => child.kill('SIGKILL'), 10000);
  t.after(() => clearTimeout(timeout));
  const exited = once(child, 'exit');
  const ready = await Promise.race([once(child, 'message'), exited.then(() => ['unexpected exit'])]);
  assert.equal(ready[0], 'replaced');
  child.kill('SIGKILL');
  await exited;
  const report = recover(w);
  assert.deepEqual(report.restored, ['.cursor/mcp.json']);
  assert.equal(fs.readFileSync(file, 'utf8'), before);
  assert.equal(createFileStore(w).owns('.cursor/mcp.json'), true);
  assert.deepEqual(fs.readdirSync(path.join(w.tmpDir, 'agent-config-locks')), []);
});

test('review: recovery of an in-process restore retains the refreshed ownership identity', (t) => {
  const w = workspace(t);
  configureAgent({ ...w, agentId: 'cursor', backend: 'tgrep' });
  const file = path.join(w.repoRoot, '.cursor/mcp.json');
  const before = fs.readFileSync(file, 'utf8');
  const store = createFileStore(w);
  store.write(file, before + '\n', { relative: '.cursor/mcp.json', expectedBefore: before });
  store.tryRestore({ file, relative: '.cursor/mcp.json' });
  pretendOwnerDied(w);
  recover(w);
  assert.equal(createFileStore(w).owns('.cursor/mcp.json'), true);
});

test('review: recovery never grants ownership to a user replacement with the original bytes', (t) => {
  const w = workspace(t);
  configureAgent({ ...w, agentId: 'cursor', backend: 'tgrep' });
  const file = path.join(w.repoRoot, '.cursor/mcp.json');
  const before = fs.readFileSync(file, 'utf8');
  createFileStore(w).write(file, before + '\n', { relative: '.cursor/mcp.json', expectedBefore: before });
  // Rename a user file into place to force a new identity, even when the
  // content happens to match the backup exactly.
  fs.writeFileSync(path.join(w.repoRoot, 'replacement'), before);
  fs.renameSync(path.join(w.repoRoot, 'replacement'), file);
  pretendOwnerDied(w);
  recover(w);
  assert.equal(fs.readFileSync(file, 'utf8'), before);
  assert.equal(createFileStore(w).owns('.cursor/mcp.json'), false);
});

test('review: backup traversal is rejected before restoring an earlier valid file', (t) => {
  const w = workspace(t);
  const store = createFileStore(w);
  const file = path.join(w.repoRoot, 'cfg.json');
  store.write(file, 'after', { relative: 'cfg.json', expectedBefore: null });
  pretendOwnerDied(w);
  const lines = fs.readFileSync(journalFile(w), 'utf8').trimEnd().split('\n').map(JSON.parse);
  lines.push({ action: 'ownership-backup', batch: lines[0].batch, backup: '../outside.bak' });
  fs.writeFileSync(journalFile(w), lines.map(JSON.stringify).join('\n') + '\n');
  assert.throws(() => recover(w), (error) => error.kind === 'journal-corrupted');
  assert.equal(fs.readFileSync(file, 'utf8'), 'after');
});

test('review: updates and recovery preserve existing permissions under a restrictive umask', { skip: process.platform === 'win32' }, (t) => {
  const w = workspace(t);
  const file = path.join(w.repoRoot, 'cfg.json');
  fs.writeFileSync(file, 'before');
  fs.chmodSync(file, 0o664);
  const originalMask = process.umask(0o077);
  try {
    createFileStore(w).write(file, 'after', { relative: 'cfg.json', expectedBefore: 'before' });
    assert.equal(fs.statSync(file).mode & 0o777, 0o664);
    pretendOwnerDied(w);
    recover(w);
    assert.equal(fs.statSync(file).mode & 0o777, 0o664);
  } finally { process.umask(originalMask); }
});

test('review: a recovery interrupted after replacement retains ownership on the next run', (t) => {
  const w = workspace(t);
  configureAgent({ ...w, agentId: 'cursor', backend: 'tgrep' });
  const file = path.join(w.repoRoot, '.cursor/mcp.json');
  const before = fs.readFileSync(file, 'utf8');
  createFileStore(w).write(file, before + '\n', { relative: '.cursor/mcp.json', expectedBefore: before });
  pretendOwnerDied(w);
  const interrupted = spawnSync(process.execPath, ['--input-type=module', '-e', `
    import fs from 'node:fs';
    import { recoverInterruptedBatch } from ${JSON.stringify(new URL('../src/fileStore.mjs', import.meta.url).href)};
    const rename = fs.renameSync;
    fs.renameSync = (source, destination) => {
      rename(source, destination);
      if (destination === process.argv[2]) process.exit(79);
    };
    recoverInterruptedBatch(JSON.parse(process.argv[1]));
  `, JSON.stringify(w), file], { encoding: 'utf8', timeout: 10000, windowsHide: true });
  assert.equal(interrupted.status, 79, interrupted.stderr);
  assert.equal(fs.readFileSync(file, 'utf8'), before);
  assert.ok(findOpenJournalBatch(w));
  recover(w);
  assert.equal(createFileStore(w).owns('.cursor/mcp.json'), true);
  assert.equal(findOpenJournalBatch(w), null);
});

test('review: missing ownership backup drops affected claims and retains unrelated ones', (t) => {
  const w = workspace(t);
  configureAgent({ ...w, agentId: 'claude', backend: 'tgrep' });
  const file = path.join(w.repoRoot, 'cfg.json');
  createFileStore(w).write(file, 'created', { relative: 'cfg.json', expectedBefore: null });
  fs.writeFileSync(file, 'user changed');
  const lines = fs.readFileSync(journalFile(w), 'utf8').trimEnd().split('\n').map(JSON.parse);
  const backup = lines.filter((entry) => entry.action === 'ownership-backup').at(-1).backup;
  fs.rmSync(path.join(w.tmpDir, 'agent-config-backups', backup));
  pretendOwnerDied(w);
  assert.equal(recover(w).ownershipRestored, false);
  assert.equal(fs.readFileSync(file, 'utf8'), 'user changed');
  assert.equal(createFileStore(w).owns('cfg.json'), false);
  assert.equal(createFileStore(w).owns('.mcp.json'), true);
});

test('review: a failed batch-begin does not append an orphan batch-end', (t) => {
  const w = workspace(t);
  const append = fs.appendFileSync;
  const injection = t.mock.method(fs, 'appendFileSync', (file, text, options) => {
    if (String(text).includes('"action":"batch-begin"')) throw new Error('begin failed');
    return append(file, text, options);
  });
  assert.throws(() => configureAgent({ ...w, agentId: 'cursor', backend: 'tgrep' }), /begin failed/);
  injection.mock.restore();
  assert.equal(findOpenJournalBatch(w), null);
  assert.equal(fs.existsSync(journalFile(w)), false);
  assert.equal(configureAgent({ ...w, agentId: 'cursor', backend: 'tgrep' }).action, 'created');
});

test('review: recovery never assumes a pid from another platform is dead', (t) => {
  const w = workspace(t);
  createFileStore(w).write(path.join(w.repoRoot, 'cfg.json'), 'after', { relative: 'cfg.json', expectedBefore: null });
  pretendOwnerDied(w);
  const foreign = process.platform === 'win32' ? 'linux' : 'win32';
  fs.writeFileSync(journalFile(w), fs.readFileSync(journalFile(w), 'utf8').replace(`"platform":"${process.platform}"`, `"platform":"${foreign}"`));
  const before = fs.readFileSync(journalFile(w));
  assert.throws(() => recover(w), (error) => error.kind === 'journal-batch-active');
  assert.deepEqual(fs.readFileSync(journalFile(w)), before);
  assert.equal(fs.readFileSync(path.join(w.repoRoot, 'cfg.json'), 'utf8'), 'after');
});

test('no journal or fully closed journal means nothing to recover', (t) => {
  const w = workspace(t);
  assert.equal(recover(w), null);
  const store = createFileStore(w);
  const file = path.join(w.repoRoot, 'cfg.json');
  store.write(file, '{"a":1}\n', { relative: 'cfg.json', expectedBefore: null });
  store.finishBatch('committed');
  assert.equal(recover(w, () => true), null);
  assert.equal(findOpenJournalBatch({ tmpDir: w.tmpDir }), null);
});

test('a crashed run leaves the batch open and the next run restores updated files', (t) => {
  const w = workspace(t);
  fs.writeFileSync(path.join(w.repoRoot, 'one.json'), 'one-before\n');
  fs.writeFileSync(path.join(w.repoRoot, 'two.json'), 'two-before\n');
  const store = createFileStore(w);
  store.write(path.join(w.repoRoot, 'one.json'), 'one-after\n', { relative: 'one.json', expectedBefore: 'one-before\n' });
  store.write(path.join(w.repoRoot, 'two.json'), 'two-after\n', { relative: 'two.json', expectedBefore: 'two-before\n' });
  // Crash: the store is abandoned without finishBatch.
  pretendOwnerDied(w);

  const report = recover(w);
  assert.deepEqual(report.restored.sort(), ['one.json', 'two.json']);
  assert.deepEqual(report.skipped, []);
  assert.equal(fs.readFileSync(path.join(w.repoRoot, 'one.json'), 'utf8'), 'one-before\n');
  assert.equal(fs.readFileSync(path.join(w.repoRoot, 'two.json'), 'utf8'), 'two-before\n');
  // Ownership was journaled by the crashed batch and is rolled back with it.
  assert.equal(fs.existsSync(path.join(w.stateDir, 'agent-config-state.json')), false);
  // Recovery is idempotent: the batch is closed by the first pass.
  assert.equal(recover(w), null);
  assert.equal(findOpenJournalBatch({ tmpDir: w.tmpDir }), null);
});

test('an update journaled but never written leaves the file untouched', (t) => {
  const w = workspace(t);
  fs.writeFileSync(path.join(w.repoRoot, 'cfg.json'), 'before\n');
  const backupDir = path.join(w.tmpDir, 'agent-config-backups');
  fs.mkdirSync(backupDir, { recursive: true });
  fs.writeFileSync(path.join(backupDir, 'deadbeef.bak'), 'before\n');
  const before = { time: 't', batch: 'b1', pid: deadPid(), action: 'batch-begin' };
  const backup = { time: 't', batch: 'b1', action: 'backup', file: 'cfg.json', backup: 'deadbeef.bak' };
  const planned = { time: 't', batch: 'b1', action: 'planned', file: 'cfg.json', operation: 'update', backup: 'deadbeef.bak', after_digest: digest('after\n'), mode: 0o644 };
  fs.mkdirSync(w.tmpDir, { recursive: true });
  fs.writeFileSync(path.join(w.tmpDir, 'agent-config-journal.jsonl'), [before, backup, planned].map((e) => JSON.stringify(e)).join('\n') + '\n');

  const report = recover(w);
  assert.deepEqual(report.restored, []);
  assert.deepEqual(report.skipped, []);
  assert.equal(fs.readFileSync(path.join(w.repoRoot, 'cfg.json'), 'utf8'), 'before\n');
  const journal = fs.readFileSync(path.join(w.tmpDir, 'agent-config-journal.jsonl'), 'utf8');
  assert.match(journal, /"result":"already-before-state"/);
});

test('a created file from a crashed batch is removed, a never-written create is kept absent', (t) => {
  const w = workspace(t);
  const store = createFileStore(w);
  store.write(path.join(w.repoRoot, 'made.json'), '{"made":true}\n', { relative: 'made.json', expectedBefore: null });
  // Also journal a create that never reached the disk, inside the same batch.
  const journalPath = path.join(w.tmpDir, 'agent-config-journal.jsonl');
  const batchId = JSON.parse(fs.readFileSync(journalPath, 'utf8').split('\n')[0]).batch;
  fs.appendFileSync(journalPath, `${JSON.stringify({ batch: batchId, action: 'planned', file: 'ghost.json', operation: 'create', backup: null, after_digest: digest('{"ghost":true}\n'), mode: null })}\n`);
  pretendOwnerDied(w);

  const report = recover(w);
  assert.deepEqual(report.removed, ['made.json']);
  assert.equal(fs.existsSync(path.join(w.repoRoot, 'made.json')), false);
  assert.equal(fs.existsSync(path.join(w.repoRoot, 'ghost.json')), false);
  assert.equal(report.ownershipRestored, true);
  assert.equal(fs.existsSync(path.join(w.stateDir, 'agent-config-state.json')), false, 'ownership created by the crashed batch is rolled back');
});

test('a deleted file from a crashed batch is restored with its journaled mode', (t) => {
  const w = workspace(t);
  const file = path.join(w.repoRoot, 'cfg.json');
  fs.writeFileSync(file, 'content\n', { mode: 0o640 });
  const store = createFileStore(w);
  store.write(file, 'changed\n', { relative: 'cfg.json', expectedBefore: 'content\n' });
  store.remove(file, { relative: 'cfg.json', expectedBefore: 'changed\n' });
  assert.equal(fs.existsSync(file), false);
  pretendOwnerDied(w);

  const report = recover(w);
  assert.deepEqual(report.restored, ['cfg.json']);
  assert.equal(fs.readFileSync(file, 'utf8'), 'content\n');
  if (process.platform !== 'win32') {
    assert.equal(fs.statSync(file).mode & 0o777, 0o640);
  }
});

test('files changed after the crash are skipped and their backups kept', (t) => {
  const w = workspace(t);
  const file = path.join(w.repoRoot, 'cfg.json');
  fs.writeFileSync(file, 'before\n');
  const store = createFileStore(w);
  store.write(file, 'after\n', { relative: 'cfg.json', expectedBefore: 'before\n' });
  store.finishBatch('committed');
  // Start a second batch and crash it, then edit the file manually.
  const store2 = createFileStore(w);
  store2.write(file, 'again\n', { relative: 'cfg.json', expectedBefore: 'after\n' });
  fs.writeFileSync(file, 'user edit\n');
  pretendOwnerDied(w);

  const report = recover(w);
  assert.deepEqual(report.skipped, [{ file: 'cfg.json', reason: 'file-changed-after-crash' }]);
  assert.equal(fs.readFileSync(file, 'utf8'), 'user edit\n');
  const backupDir = path.join(w.tmpDir, 'agent-config-backups');
  assert.ok(fs.readdirSync(backupDir).some((name) => name.endsWith('.bak') && fs.readFileSync(path.join(backupDir, name), 'utf8') === 'after\n'), 'backup of the interrupted batch is kept');
});

test('a live batch owner aborts recovery with zero writes', (t) => {
  const w = workspace(t);
  const file = path.join(w.repoRoot, 'cfg.json');
  fs.writeFileSync(file, 'before\n');
  const store = createFileStore(w);
  store.write(file, 'after\n', { relative: 'cfg.json', expectedBefore: 'before\n' });

  assert.throws(() => recover(w, () => false), /still running/);
  assert.equal(fs.readFileSync(file, 'utf8'), 'after\n');
  // The current process itself also counts as live.
  assert.throws(() => recover(w), /still running/);
});

test('a damaged journal aborts before any write; a torn last line is tolerated', (t) => {
  const w = workspace(t);
  fs.mkdirSync(w.tmpDir, { recursive: true });
  const journalPath = path.join(w.tmpDir, 'agent-config-journal.jsonl');
  fs.writeFileSync(journalPath, '{"time":"t","action":"batch-begin","batch":"b1","pid":1}\nnot json\n{"time":"t","action":"updated","file":"a.json"}\n');
  assert.throws(() => recover(w), /unreadable journal line 2/);

  fs.writeFileSync(journalPath, `{"time":"t","action":"batch-begin","batch":"b1","pid":${deadPid()}}\n{"time":"t","batch":"b1","action":"planned","file":"a.json","operation":"create","after_digest":"x"`);
  const report = recover(w);
  assert.equal(report.tornTail, true);
  assert.deepEqual(report.removed, []);
});

test('two open batch-begins are refused as journal corruption', (t) => {
  const w = workspace(t);
  fs.mkdirSync(w.tmpDir, { recursive: true });
  fs.writeFileSync(path.join(w.tmpDir, 'agent-config-journal.jsonl'),
    '{"action":"batch-begin","batch":"b1","pid":1}\n{"action":"batch-begin","batch":"b2","pid":2}\n');
  assert.throws(() => recover(w, () => true), /left open/);
});

test('an in-process rollback closes the batch, so the next run recovers nothing', (t) => {
  const w = workspace(t);
  const file = path.join(w.repoRoot, 'cfg.json');
  fs.writeFileSync(file, 'before\n');
  const store = createFileStore(w);
  const step = { file, relative: 'cfg.json' };
  store.write(file, 'after\n', { relative: 'cfg.json', expectedBefore: 'before\n' });
  assert.equal(store.tryRestore(step), true);
  assert.equal(store.finishBatch('rolled-back'), true);
  assert.equal(recover(w), null);
});

test('runAgentConfigBatch recovers a crashed batch before planning the new one', (t) => {
  const w = workspace(t);
  // Simulate a crashed configure of cursor...
  const crashed = planAgentConfigChanges({ ...w, operations: [{ type: 'configure', agentId: 'cursor', backend: 'tgrep' }] });
  const store = createFileStore(w);
  for (const step of crashed.steps) {
    store.write(step.file, step.after, { relative: step.relative, expectedBefore: step.before });
  }
  pretendOwnerDied(w);
  assert.ok(findOpenJournalBatch({ tmpDir: w.tmpDir }));

  // ...then a later healthy run configures claude and recovers the crash first.
  const result = runAgentConfigBatch({ ...w, operations: [{ type: 'configure', agentId: 'claude', backend: 'tgrep' }] });
  assert.equal(result.applied, true);
  assert.deepEqual(result.recovery.removed.sort(), ['.cursor/mcp.json']);
  assert.equal(fs.existsSync(path.join(w.repoRoot, '.cursor/mcp.json')), false);
  assert.ok(fs.existsSync(path.join(w.repoRoot, '.mcp.json')));
  assert.equal(findOpenJournalBatch({ tmpDir: w.tmpDir }), null);
  const journal = fs.readFileSync(path.join(w.tmpDir, 'agent-config-journal.jsonl'), 'utf8');
  assert.match(journal, /"status":"crash-recovered"/);
  assert.match(journal, /"status":"committed"/);
});

test('recovery reruns are idempotent when interrupted themselves', (t) => {
  const w = workspace(t);
  fs.writeFileSync(path.join(w.repoRoot, 'one.json'), 'one-before\n');
  const store = createFileStore(w);
  store.write(path.join(w.repoRoot, 'one.json'), 'one-after\n', { relative: 'one.json', expectedBefore: 'one-before\n' });
  pretendOwnerDied(w);

  const first = recover(w);
  assert.deepEqual(first.restored, ['one.json']);
  // Simulate the recovery itself crashing after restoring the file but
  // before its batch-end entry: the next pass no-ops and closes the batch.
  const journalPath = path.join(w.tmpDir, 'agent-config-journal.jsonl');
  const lines = fs.readFileSync(journalPath, 'utf8').split('\n').filter((line) => line.trim() !== '' && !line.includes('"status":"crash-recovered"'));
  fs.writeFileSync(journalPath, lines.map((line) => `${line}\n`).join(''));
  const second = recover(w);
  assert.deepEqual(second.restored, []);
  assert.deepEqual(second.skipped, []);
  assert.equal(fs.readFileSync(path.join(w.repoRoot, 'one.json'), 'utf8'), 'one-before\n');
  assert.equal(recover(w), null);
});

test('a torn tail is dropped before appending so later runs still parse the journal', (t) => {
  const w = workspace(t);
  fs.writeFileSync(path.join(w.repoRoot, 'made.json'), '{"made":true}\n');
  const journalPath = path.join(w.tmpDir, 'agent-config-journal.jsonl');
  fs.mkdirSync(w.tmpDir, { recursive: true });
  const pid = deadPid();
  fs.writeFileSync(journalPath, [
    JSON.stringify({ time: 't', action: 'batch-begin', batch: 'b1', pid }),
    JSON.stringify({ time: 't', batch: 'b1', action: 'planned', file: 'made.json', operation: 'create', backup: null, after_digest: digest('{"made":true}\n'), mode: null }),
    '{"time":"t","batch":"b1","action":"planned","file":"trunc', // torn mid-append
  ].join('\n'));

  const report = recover(w);
  assert.equal(report.tornTail, true);
  assert.deepEqual(report.removed, ['made.json']);
  assert.equal(fs.existsSync(path.join(w.repoRoot, 'made.json')), false);
  const text = fs.readFileSync(journalPath, 'utf8');
  assert.ok(!text.includes('trunc'), 'torn bytes are gone');
  // The batch is closed; future recoveries and batches parse the journal.
  assert.equal(recover(w), null);
  const store = createFileStore(w);
  store.write(path.join(w.repoRoot, 'next.json'), '{}\n', { relative: 'next.json', expectedBefore: null });
  store.finishBatch('committed');
  assert.equal(recover(w), null, 'no journal-corrupted abort after appending past the old tear');
});

test('a dry-run reports but never recovers an interrupted batch', (t) => {
  const w = workspace(t);
  const crashed = planAgentConfigChanges({ ...w, operations: [{ type: 'configure', agentId: 'cursor', backend: 'tgrep' }] });
  const store = createFileStore(w);
  for (const step of crashed.steps) {
    store.write(step.file, step.after, { relative: step.relative, expectedBefore: step.before });
  }
  pretendOwnerDied(w);

  const result = runAgentConfigBatch({ ...w, operations: [{ type: 'configure', agentId: 'claude', backend: 'tgrep' }], dryRun: true });
  assert.equal(result.applied, false);
  assert.equal(result.recovery, null);
  assert.equal(fs.existsSync(path.join(w.repoRoot, '.cursor/mcp.json')), true, 'dry-run must not roll back the crashed write');
  assert.equal(fs.existsSync(path.join(w.repoRoot, '.mcp.json')), false, 'dry-run must not write');
  assert.ok(findOpenJournalBatch({ tmpDir: w.tmpDir }), 'batch stays open for the next real run');
});

test('the compat configureAgent API recovers an interrupted batch before planning', (t) => {
  const w = workspace(t);
  const crashed = planAgentConfigChanges({ ...w, operations: [{ type: 'configure', agentId: 'cursor', backend: 'tgrep' }] });
  const store = createFileStore(w);
  for (const step of crashed.steps) {
    store.write(step.file, step.after, { relative: step.relative, expectedBefore: step.before });
  }
  pretendOwnerDied(w);

  const result = configureAgent({ ...w, agentId: 'claude', backend: 'tgrep' });
  assert.equal(result.action, 'created');
  assert.equal(fs.existsSync(path.join(w.repoRoot, '.cursor/mcp.json')), false, 'crashed creation rolled back');
  assert.equal(fs.existsSync(path.join(w.repoRoot, '.mcp.json')), true);
  assert.equal(findOpenJournalBatch({ tmpDir: w.tmpDir }), null);
});

test('a journaled update whose backup file is gone is skipped, not restored from nothing', (t) => {
  const w = workspace(t);
  const file = path.join(w.repoRoot, 'cfg.json');
  fs.writeFileSync(file, 'after\n');
  const journalPath = path.join(w.tmpDir, 'agent-config-journal.jsonl');
  fs.mkdirSync(w.tmpDir, { recursive: true });
  const pid = deadPid();
  fs.writeFileSync(journalPath, [
    JSON.stringify({ time: 't', action: 'batch-begin', batch: 'b1', pid }),
    JSON.stringify({ time: 't', batch: 'b1', action: 'backup', file: 'cfg.json', backup: 'gone.bak' }),
    JSON.stringify({ time: 't', batch: 'b1', action: 'planned', file: 'cfg.json', operation: 'update', backup: 'gone.bak', after_digest: digest('after\n'), mode: 0o600 }),
    '',
  ].join('\n'));

  const report = recover(w);
  assert.deepEqual(report.restored, []);
  assert.deepEqual(report.skipped, [{ file: 'cfg.json', reason: 'missing-backup' }]);
  assert.equal(fs.readFileSync(file, 'utf8'), 'after\n', 'no backup means no rollback source; the file is left alone');
  assert.equal(recover(w), null);
});

test('journaled paths escaping the repository are skipped without touching them', (t) => {
  const w = workspace(t);
  const outside = path.join(w.repoRoot, '..', `outside-${path.basename(w.repoRoot)}.json`);
  fs.writeFileSync(outside, 'foreign\n');
  t.after(() => fs.rmSync(outside, { force: true }));
  const journalPath = path.join(w.tmpDir, 'agent-config-journal.jsonl');
  fs.mkdirSync(w.tmpDir, { recursive: true });
  const backupDir = path.join(w.tmpDir, 'agent-config-backups');
  fs.mkdirSync(backupDir, { recursive: true });
  fs.writeFileSync(path.join(backupDir, 'b.bak'), 'before\n');
  const pid = deadPid();
  const relative = path.basename(outside);
  fs.writeFileSync(journalPath, [
    JSON.stringify({ time: 't', action: 'batch-begin', batch: 'b1', pid }),
    JSON.stringify({ time: 't', batch: 'b1', action: 'backup', file: `../${relative}`, backup: 'b.bak' }),
    JSON.stringify({ time: 't', batch: 'b1', action: 'planned', file: `../${relative}`, operation: 'update', backup: 'b.bak', after_digest: digest('after\n'), mode: 0o600 }),
    '',
  ].join('\n'));

  const report = recover(w);
  assert.deepEqual(report.restored, []);
  assert.deepEqual(report.skipped, [{ file: `../${relative}`, reason: 'outside-repo' }]);
  assert.equal(fs.readFileSync(outside, 'utf8'), 'foreign\n');
});

test('recovery refuses to write through a directory junction swapped in after the crash', (t) => {
  const w = workspace(t);
  const outsideRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-crash-outside-'));
  t.after(() => fs.rmSync(outsideRoot, { recursive: true, force: true }));
  // The batch updated link/cfg.json while link was a real directory.
  fs.mkdirSync(path.join(w.repoRoot, 'link'));
  fs.writeFileSync(path.join(w.repoRoot, 'link', 'cfg.json'), 'after\n');
  const backupDir = path.join(w.tmpDir, 'agent-config-backups');
  fs.mkdirSync(backupDir, { recursive: true });
  fs.writeFileSync(path.join(backupDir, 'b.bak'), 'before\n');
  const journalPath = path.join(w.tmpDir, 'agent-config-journal.jsonl');
  const pid = deadPid();
  fs.writeFileSync(journalPath, [
    JSON.stringify({ time: 't', action: 'batch-begin', batch: 'b1', pid }),
    JSON.stringify({ time: 't', batch: 'b1', action: 'backup', file: 'link/cfg.json', backup: 'b.bak' }),
    JSON.stringify({ time: 't', batch: 'b1', action: 'planned', file: 'link/cfg.json', operation: 'update', backup: 'b.bak', after_digest: digest('after\n'), mode: 0o600 }),
    '',
  ].join('\n'));
  // After the crash the directory was replaced by a link escaping the repo,
  // and the escape target happens to hold exactly the post-batch content.
  fs.rmSync(path.join(w.repoRoot, 'link'), { recursive: true });
  fs.writeFileSync(path.join(outsideRoot, 'cfg.json'), 'after\n');
  fs.symlinkSync(outsideRoot, path.join(w.repoRoot, 'link'), 'junction');

  const report = recover(w);
  assert.deepEqual(report.restored, []);
  assert.deepEqual(report.skipped, [{ file: 'link/cfg.json', reason: 'outside-repo' }]);
  assert.equal(fs.readFileSync(path.join(outsideRoot, 'cfg.json'), 'utf8'), 'after\n', 'nothing written through the junction');
});

test('a batch-end for a different batch and interleaved batch entries are corruption', (t) => {
  const w = workspace(t);
  const journalPath = path.join(w.tmpDir, 'agent-config-journal.jsonl');
  fs.mkdirSync(w.tmpDir, { recursive: true });
  fs.writeFileSync(journalPath, '{"action":"batch-begin","batch":"b1","pid":1}\n{"action":"batch-end","batch":"b2"}\n');
  assert.throws(() => recover(w, () => true), /does not close open batch/);

  fs.writeFileSync(journalPath, '{"action":"batch-begin","batch":"b1","pid":1}\n{"action":"planned","batch":"b2","file":"a.json","operation":"create"}\n');
  assert.throws(() => recover(w, () => true), /interleaved/);
  assert.throws(() => findOpenJournalBatch({ tmpDir: w.tmpDir }), /interleaved/);
});

test('a backup-only entry has no planned mutation and leaves the file untouched', (t) => {
  const w = workspace(t);
  const file = path.join(w.repoRoot, 'cfg.json');
  fs.writeFileSync(file, 'current\n');
  const journalPath = path.join(w.tmpDir, 'agent-config-journal.jsonl');
  fs.mkdirSync(w.tmpDir, { recursive: true });
  const pid = deadPid();
  fs.writeFileSync(journalPath, [
    JSON.stringify({ time: 't', action: 'batch-begin', batch: 'b1', pid }),
    JSON.stringify({ time: 't', batch: 'b1', action: 'backup', file: 'cfg.json', backup: 'b.bak' }),
    '',
  ].join('\n'));

  const report = recover(w);
  assert.deepEqual(report.restored, []);
  assert.deepEqual(report.removed, []);
  assert.deepEqual(report.skipped, []);
  assert.equal(fs.readFileSync(file, 'utf8'), 'current\n');
  assert.equal(recover(w), null, 'batch closed even without recoverable files');
});

test('ownership restore failure keeps the file rollback and reports the failure', (t) => {
  const w = workspace(t);
  // A pre-existing ownership record gives the batch a named snapshot backup.
  fs.mkdirSync(w.stateDir, { recursive: true });
  fs.writeFileSync(path.join(w.stateDir, 'agent-config-state.json'), '{"files":{}}\n');
  const file = path.join(w.repoRoot, 'cfg.json');
  fs.writeFileSync(file, 'before\n');
  const store = createFileStore(w);
  store.write(file, 'after\n', { relative: 'cfg.json', expectedBefore: 'before\n' });
  // Remove the journaled ownership snapshot so its restore must fail.
  const journalPath = path.join(w.tmpDir, 'agent-config-journal.jsonl');
  const ownershipBackup = JSON.parse(fs.readFileSync(journalPath, 'utf8').split('\n').find((line) => line.includes('ownership-backup'))).backup;
  fs.rmSync(path.join(w.tmpDir, 'agent-config-backups', ownershipBackup));
  pretendOwnerDied(w);

  const report = recover(w);
  assert.deepEqual(report.restored, ['cfg.json']);
  assert.equal(report.ownershipRestored, false);
  assert.equal(fs.readFileSync(file, 'utf8'), 'before\n');
  assert.equal(recover(w), null);
});

test('a symlinked config file is skipped instead of removed or replaced', { skip: process.platform === 'win32' }, (t) => {
  const w = workspace(t);
  const real = path.join(w.repoRoot, 'real.json');
  fs.writeFileSync(real, 'real\n');
  const link = path.join(w.repoRoot, 'cfg.json');
  fs.symlinkSync(real, link);
  const backupDir = path.join(w.tmpDir, 'agent-config-backups');
  fs.mkdirSync(backupDir, { recursive: true });
  fs.writeFileSync(path.join(backupDir, 'b.bak'), 'before\n');
  const journalPath = path.join(w.tmpDir, 'agent-config-journal.jsonl');
  fs.mkdirSync(w.tmpDir, { recursive: true });
  const pid = deadPid();
  fs.writeFileSync(journalPath, [
    JSON.stringify({ time: 't', action: 'batch-begin', batch: 'b1', pid }),
    JSON.stringify({ time: 't', batch: 'b1', action: 'backup', file: 'cfg.json', backup: 'b.bak' }),
    JSON.stringify({ time: 't', batch: 'b1', action: 'planned', file: 'cfg.json', operation: 'update', backup: 'b.bak', after_digest: digest('after\n'), mode: 0o600 }),
    '',
  ].join('\n'));

  const report = recover(w);
  assert.deepEqual(report.restored, []);
  assert.deepEqual(report.skipped, [{ file: 'cfg.json', reason: 'symlink-target' }]);
  assert.equal(fs.lstatSync(link).isSymbolicLink(), true);
  assert.equal(fs.readFileSync(real, 'utf8'), 'real\n');
});
