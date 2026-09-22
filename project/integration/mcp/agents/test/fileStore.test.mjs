import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { createFileStore } from '../src/fileStore.mjs';

function tmpWorkspace() {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-filestore-'));
  return { root, stateDir: path.join(root, 'state'), tmpDir: path.join(root, 'tmp') };
}

function storeFor(workspace) {
  return createFileStore({ repoRoot: workspace.root, stateDir: workspace.stateDir, tmpDir: workspace.tmpDir });
}

test('ownership accepts Deno millisecond and legacy Node fractional birth times but rejects a replaced file', () => {
  const workspace = tmpWorkspace();
  try {
    const store = storeFor(workspace);
    const file = path.join(workspace.root, 'cfg.json');
    store.write(file, '{}', { relative: 'cfg.json', expectedBefore: null });
    const stateFile = path.join(workspace.stateDir, 'agent-config-state.json');
    const state = JSON.parse(fs.readFileSync(stateFile));
    const stat = fs.statSync(file);
    for (const time of [Math.trunc(stat.birthtimeMs), stat.birthtimeMs]) {
      state.files['cfg.json'].file_id = `${stat.dev}:${stat.ino}:${time}`;
      fs.writeFileSync(stateFile, JSON.stringify(state));
      assert.equal(store.owns('cfg.json'), true);
    }
    state.files['cfg.json'].file_id = `${stat.dev}:${stat.ino}:${Math.trunc(stat.birthtimeMs) + 1}`;
    fs.writeFileSync(stateFile, JSON.stringify(state));
    assert.equal(store.owns('cfg.json'), false, 'a different creation time is not ours');
    state.files['cfg.json'].file_id = `${stat.dev}:${stat.ino}:${stat.birthtimeMs}`;
    fs.writeFileSync(stateFile, JSON.stringify(state));
    fs.renameSync(file, file + '.original');
    fs.writeFileSync(file, '{}');
    assert.equal(store.owns('cfg.json'), false, 'same bytes in another file do not prove ownership');
  } finally { fs.rmSync(workspace.root, { recursive: true, force: true }); }
});

test('write creates the file, records ownership, and is idempotent', () => {
  const workspace = tmpWorkspace();
  try {
    const store = storeFor(workspace);
    const file = path.join(workspace.root, 'cfg.json');
    assert.equal(store.write(file, '{"a":1}\n', { relative: 'cfg.json', expectedBefore: null }), 'created');
    assert.equal(fs.readFileSync(file, 'utf8'), '{"a":1}\n');
    assert.equal(store.owns('cfg.json'), true);

    // Same content again: no write, no new backup.
    assert.equal(store.write(file, '{"a":1}\n', { relative: 'cfg.json', expectedBefore: '{"a":1}\n' }), 'unchanged');
    assert.equal(fs.readdirSync(path.join(workspace.tmpDir, 'agent-config-backups')).length, 0);

    // Changed content: backup taken, update written.
    assert.equal(store.write(file, '{"a":2}\n', { relative: 'cfg.json', expectedBefore: '{"a":1}\n' }), 'updated');
    assert.equal(fs.readFileSync(file, 'utf8'), '{"a":2}\n');
    assert.equal(fs.readdirSync(path.join(workspace.tmpDir, 'agent-config-backups')).length, 1);
  } finally {
    fs.rmSync(workspace.root, { recursive: true, force: true });
  }
});

test('write refuses when the file changed concurrently since the plan', () => {
  const workspace = tmpWorkspace();
  try {
    const store = storeFor(workspace);
    const file = path.join(workspace.root, 'cfg.json');
    fs.writeFileSync(file, '{"a":1}\n', 'utf8');
    fs.writeFileSync(file, '{"a":0}\n', 'utf8'); // someone edits between plan and apply
    assert.throws(
      () => store.write(file, '{"a":2}\n', { relative: 'cfg.json', expectedBefore: '{"a":1}\n' }),
      (error) => error.kind === 'concurrent-modification',
    );
    assert.equal(fs.readFileSync(file, 'utf8'), '{"a":0}\n', 'concurrent edit untouched');
  } finally {
    fs.rmSync(workspace.root, { recursive: true, force: true });
  }
});

test('remove deletes the file and clears ownership; tryRestore undoes writes', () => {
  const workspace = tmpWorkspace();
  try {
    const store = storeFor(workspace);
    const file = path.join(workspace.root, 'cfg.json');
    const step = { file, relative: 'cfg.json', before: '{"user":true}\n', after: '{"user":true,"ours":1}\n' };
    fs.writeFileSync(file, step.before, 'utf8');
    store.write(file, step.after, { relative: step.relative, expectedBefore: step.before });

    assert.equal(store.tryRestore(step), true);
    assert.equal(fs.readFileSync(file, 'utf8'), step.before, 'write rolled back to the backed-up bytes');

    // A created file (nothing existed before) disappears on restore.
    const created = { file: path.join(workspace.root, 'new.json'), relative: 'new.json', before: null, after: '{}\n' };
    store.write(created.file, created.after, { relative: created.relative, expectedBefore: null });
    assert.equal(store.tryRestore(created), true);
    assert.equal(fs.existsSync(created.file), false);

    // Remove + ownership.
    store.write(file, step.after, { relative: step.relative, expectedBefore: step.before });
    assert.equal(store.owns(step.relative), false, 'pre-existing file is never owned');
    const removal = { file, relative: step.relative, before: step.after, after: null, deleteFile: true };
    assert.equal(store.remove(file, { relative: step.relative, expectedBefore: step.after }), 'removed-file');
    assert.equal(fs.existsSync(file), false);
    assert.equal(store.tryRestore(removal), true);
    assert.equal(fs.readFileSync(file, 'utf8'), step.after, 'deletion rolled back');
  } finally {
    fs.rmSync(workspace.root, { recursive: true, force: true });
  }
});

test('tryRestore refuses to overwrite content that changed after our write', () => {
  const workspace = tmpWorkspace();
  try {
    const store = storeFor(workspace);
    const file = path.join(workspace.root, 'cfg.json');
    const step = { file, relative: 'cfg.json', before: '{"v":1}\n', after: '{"v":2}\n' };
    fs.writeFileSync(file, step.before, 'utf8');
    store.write(file, step.after, { relative: step.relative, expectedBefore: step.before });
    fs.writeFileSync(file, '{"v":3}\n', 'utf8'); // user edits after our write
    assert.equal(store.tryRestore(step), false);
    assert.equal(fs.readFileSync(file, 'utf8'), '{"v":3}\n', 'newer content preserved');
  } finally {
    fs.rmSync(workspace.root, { recursive: true, force: true });
  }
});

test('writes outside the repository root are refused', () => {
  const workspace = tmpWorkspace();
  const outside = tmpWorkspace();
  try {
    const store = storeFor(workspace);
    const file = path.join(outside.root, 'cfg.json');
    assert.throws(
      () => store.write(file, '{}\n', { relative: '../escape.json', expectedBefore: null }),
      (error) => error.kind === 'outside-repo',
    );
    assert.equal(fs.existsSync(file), false);
  } finally {
    fs.rmSync(workspace.root, { recursive: true, force: true });
    fs.rmSync(outside.root, { recursive: true, force: true });
  }
});

test('the journal records paths and actions, never config values', () => {
  const workspace = tmpWorkspace();
  try {
    const store = storeFor(workspace);
    const file = path.join(workspace.root, 'cfg.json');
    const secret = 'SUPER_SECRET_COMMAND_VALUE';
    fs.writeFileSync(file, `{"x":"${secret}"}\n`, 'utf8');
    store.write(file, '{"x":"other"}\n', { relative: 'cfg.json', expectedBefore: `{"x":"${secret}"}\n` });
    const journal = fs.readFileSync(path.join(workspace.tmpDir, 'agent-config-journal.jsonl'), 'utf8');
    assert.doesNotMatch(journal, new RegExp(secret), 'no config values copied into the journal');
    assert.match(journal, /"action":"updated"/);
    assert.match(journal, /"file":"cfg.json"/);
    const backupFiles = fs.readdirSync(path.join(workspace.tmpDir, 'agent-config-backups'));
    assert.equal(backupFiles.length, 1);
  } finally {
    fs.rmSync(workspace.root, { recursive: true, force: true });
  }
});
