import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { IndexInUse } from '../src/errors.mjs';
import { StateStore, ToolInstanceLock, currentIdentity } from '../src/state.mjs';

function tmpDir() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-state-'));
}

test('StateStore writes atomically and reads back', () => {
  const dir = tmpDir();
  try {
    const store = new StateStore(path.join(dir, 'wrapper-state.json'));
    store.write({ state: 'ready' });
    assert.deepEqual(store.read(), { state: 'ready' });
    store.write({ state: 'stopped' });
    assert.deepEqual(store.read(), { state: 'stopped' });
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

test('ToolInstanceLock is exclusive per tool+directory', () => {
  const dir = tmpDir();
  try {
    const first = new ToolInstanceLock(dir, 'tgrep');
    const second = new ToolInstanceLock(dir, 'tgrep');
    const otherTool = new ToolInstanceLock(dir, 'codegraph');

    first.acquire(currentIdentity(), 'C:\\repo', 'starting');
    assert.throws(() => second.acquire(currentIdentity(), 'C:\\repo', 'starting'), IndexInUse);
    // A different tool in the same directory has its own lock file.
    otherTool.acquire(currentIdentity(), 'C:\\repo', 'starting');

    first.updateStage('ready');
    assert.equal(first.readHolder().stage, 'ready');

    first.release();
    otherTool.release();
    // After release the lock is acquirable again.
    second.acquire(currentIdentity(), 'C:\\repo', 'starting');
    second.release();
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

test('ToolInstanceLock steals a dead holder once', () => {
  const dir = tmpDir();
  try {
    const store = new StateStore(path.join(dir, 'tgrep.holder.json'));
    // A holder pid that does not exist: pid 0x7fffffff is far beyond typical ranges.
    store.write({ identity: { pid: 2147483000, instance_id: 'dead' }, repo_root: 'x', stage: 'ready' });
    fs.writeFileSync(path.join(dir, 'tgrep.instance.lock'), '');

    const lock = new ToolInstanceLock(dir, 'tgrep');
    lock.acquire(currentIdentity(), 'C:\\repo', 'starting');
    lock.release();
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
