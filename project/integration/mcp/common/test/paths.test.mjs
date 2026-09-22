import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import test from 'node:test';

import {
  WorkspacePaths,
  deriveRepoRoot,
  isWithin,
  validateRelativeScope,
  validateRepoRoot,
  workspaceId,
  resolveBuildDir,
} from '../src/paths.mjs';

test('build directory honors JSONC settings, clangd fallback and explicit override', (t) => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-build-settings-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  fs.mkdirSync(path.join(root, '.vscode'));
  const file = path.join(root, '.vscode/settings.json');
  fs.writeFileSync(file, '{ // local build\n "cmake.buildDirectory": "${workspaceFolder}/output with spaces", }');
  assert.equal(resolveBuildDir(root), path.join(root, 'output with spaces'));
  assert.equal(resolveBuildDir(root, 'explicit'), path.join(root, 'explicit'));
  fs.writeFileSync(file, '{"clangd.arguments": ["--compile-commands-dir=${workspaceFolder}/compile"]}');
  assert.equal(resolveBuildDir(root), path.join(root, 'compile'));
  fs.writeFileSync(file, '{"cmake.buildDirectory": "${unknown}/build"}');
  assert.throws(() => resolveBuildDir(root), /unsupported build-directory variable/);
  fs.writeFileSync(file, '{broken');
  assert.throws(() => resolveBuildDir(root), /invalid JSON/);
});

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

/** A scratch Git worktree; the toolkit can be anywhere inside it. */
function markedRoot() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-root-'));
  fs.mkdirSync(path.join(dir, 'tools', 'mcp'), { recursive: true });
  fs.writeFileSync(path.join(dir, 'tools', 'mcp', 'setup.js'), '// marker\n');
  fs.mkdirSync(path.join(dir, '.git'));
  return dir;
}

test('validateRepoRoot returns the canonical directory', () => {
  const dir = markedRoot();
  try {
    assert.equal(validateRepoRoot(dir), fs.realpathSync(dir));
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

test('validateRepoRoot accepts an unmarked workspace but rejects a non-directory', () => {
  const bare = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-root-bare-'));
  try {
    assert.equal(validateRepoRoot(bare), fs.realpathSync(bare));
    const file = path.join(bare, 'file');
    fs.writeFileSync(file, 'x');
    assert.throws(() => validateRepoRoot(file), /not a directory/);
  } finally {
    fs.rmSync(bare, { recursive: true, force: true });
  }
});

test('deriveRepoRoot walks up from a nested entry module and honors the explicit root', () => {
  const dir = markedRoot();
  try {
    const entry = path.join(dir, 'tools', 'mcp', 'tgrep', 'src');
    fs.mkdirSync(entry, { recursive: true });
    const moduleFile = path.join(entry, 'server.mjs');
    fs.writeFileSync(moduleFile, '// entry\n');
    assert.equal(deriveRepoRoot(pathToFileURL(moduleFile), null), fs.realpathSync(dir));
    assert.equal(deriveRepoRoot(pathToFileURL(moduleFile), dir), fs.realpathSync(dir));
    const bare = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-root-bare-'));
    try {
      assert.equal(deriveRepoRoot(pathToFileURL(moduleFile), bare), fs.realpathSync(bare), 'an explicit workspace does not need to contain the toolkit');
    } finally {
      fs.rmSync(bare, { recursive: true, force: true });
    }
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

test('deriveRepoRoot detects a project more than eight directories above the entry', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-root-lost-'));
  try {
    fs.mkdirSync(path.join(dir, '.git'));
    const entry = path.join(dir, ...Array.from({ length: 8 }, (_, i) => `level-${i}`));
    fs.mkdirSync(entry, { recursive: true });
    const moduleFile = path.join(entry, 'server.mjs');
    fs.writeFileSync(moduleFile, '// entry\n');
    assert.equal(deriveRepoRoot(pathToFileURL(moduleFile), null), fs.realpathSync(dir));
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
});
