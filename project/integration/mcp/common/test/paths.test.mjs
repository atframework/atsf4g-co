import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  WorkspacePaths,
  isWithin,
  validateRelativeScope,
  workspaceId,
} from '../src/paths.mjs';

test('validateRelativeScope accepts and normalizes plain subdirectories', () => {
  assert.equal(validateRelativeScope('src//component/'), 'src/component');
  assert.equal(validateRelativeScope('  '), '');
  assert.equal(validateRelativeScope(null), '');
});

test('validateRelativeScope rejects escapes and absolute forms', () => {
  assert.throws(() => validateRelativeScope('..'));
  assert.throws(() => validateRelativeScope('src/../..'));
  assert.throws(() => validateRelativeScope('/etc'));
  assert.throws(() => validateRelativeScope('\\server\\share'));
  assert.throws(() => validateRelativeScope('src\\sub'));
  assert.throws(() => validateRelativeScope('C:\\repo'));
});

test('isWithin uses path components, not prefixes', () => {
  const root = path.resolve('/repo');
  assert.equal(isWithin(path.resolve('/repo/src'), root), true);
  assert.equal(isWithin(path.resolve('/repo'), root), true);
  assert.equal(isWithin(path.resolve('/repo-x/src'), root), false);
  assert.equal(isWithin(path.resolve('/outside/repo'), root), false);
});

test('workspaceId is stable and distinct', () => {
  assert.equal(workspaceId('C:\\a\\b'), workspaceId('C:\\a\\b'));
  assert.notEqual(workspaceId('C:\\a\\b'), workspaceId('C:\\a\\c'));
});

test('WorkspacePaths nests tool state under build dir', () => {
  const paths = new WorkspacePaths(path.resolve('/repo'), path.resolve('/repo/build'));
  const toolDir = paths.toolStateDir('tgrep');
  assert.ok(toolDir.includes(path.join('integration', 'mcp', 'state', 'tgrep')));
  assert.ok(toolDir.startsWith(path.resolve('/repo/build')));
});

test('WorkspacePaths.ensureDirs creates the tree', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-paths-'));
  try {
    const paths = new WorkspacePaths(dir, path.join(dir, 'build'));
    paths.ensureDirs();
    assert.ok(fs.statSync(paths.stateDir, { throwIfNoEntry: false })?.isDirectory());
    assert.ok(fs.statSync(paths.runtimeDir, { throwIfNoEntry: false })?.isDirectory());
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
