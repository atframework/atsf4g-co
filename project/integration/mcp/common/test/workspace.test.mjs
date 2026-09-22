import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { detectWorkspace, projectInfo, resolveBuildDir, selectCodegraphIndex, platformName, INTEGRATION_ROOT } from '../src/paths.mjs';

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-discovery-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const write = (file, text = '') => { fs.mkdirSync(path.dirname(path.join(root, file)), { recursive: true }); fs.writeFileSync(path.join(root, file), text); };
  return { root, write };
}

test('discovery selects the nearest project and explicit roots override markers', t => {
  const { root, write } = fixture(t);
  write('.git', 'gitdir: elsewhere');
  write('Client/Game/Game.uproject', '{}');
  write('Client/Game/Source/Probe.cpp');
  assert.equal(detectWorkspace(path.join(root, 'Client/Game/Source')).root, path.join(root, 'Client/Game'));
  assert.equal(detectWorkspace(root).root, root);
  assert.equal(detectWorkspace(path.join(root, 'Client/Game/Source'), { explicit: root }).root, root);
});

test('unmarked directories fall back to the execution directory within the search boundary', t => {
  const { root, write } = fixture(t);
  write('loose files/example.txt');
  const cwd = path.join(root, 'loose files');
  assert.deepEqual(detectWorkspace(cwd, { stopAt: root }), { root: cwd, detected: false, reason: 'cwd' });
  write('.p4ignore', '*.log');
  assert.equal(detectWorkspace(cwd, { stopAt: root }).root, root);
});

test('project metadata and cache settings adapt to Unreal, CMake and manifests', t => {
  const { root, write } = fixture(t);
  write('Game.uproject', '{}');
  assert.deepEqual(projectInfo(root), { name: 'Game', slug: 'game', isUnreal: true });
  assert.equal(resolveBuildDir(root), path.join(root, 'Intermediate/AI/MCP'));
  fs.unlinkSync(path.join(root, 'Game.uproject'));
  write('CMakeLists.txt', '# project(NotThis)\nproject(\n "Example Server" LANGUAGES CXX)\n');
  assert.equal(projectInfo(root).name, 'Example Server');
  write('out/CMakeCache.txt', `CMAKE_HOME_DIRECTORY:INTERNAL=${root.replaceAll('\\', '/')}\n`);
  assert.equal(resolveBuildDir(root), path.join(root, 'out'));
  write('package.json', '{"name":"@demo/frontend"}');
  assert.equal(projectInfo(root).slug, 'demo-frontend');
  write('package.json', '{broken');
  assert.equal(projectInfo(root).name, 'Example Server');
});

test('toolkit package manifests are not mistaken for the host project', () => {
  const selected = detectWorkspace(path.join(INTEGRATION_ROOT, 'common/src'));
  assert.equal(selected.reason === 'package.json' && selected.root.startsWith(INTEGRATION_ROOT), false);
});

test('manifest names stay within their sections and unresolved names fall back to the directory', t => {
  const { root, write } = fixture(t);
  write('Cargo.toml', '[package]\nversion="1"\n[[bin]]\nname="not-the-project"\n');
  assert.equal(projectInfo(root).name, path.basename(root));
  write('pyproject.toml', "[project]\ndescription = '''\nname = 'not-the-project'\n'''\nname = 'python-demo'\n");
  assert.equal(projectInfo(root).name, 'python-demo');
  write('Cargo.toml', "[package]\n# description = '''not a multiline string\nname = 'rust-demo'\n");
  assert.equal(projectInfo(root).name, 'rust-demo');
});

test('index names follow the project and reuse exactly one same-platform legacy index', t => {
  const { root, write } = fixture(t);
  write('package.json', '{"name":"demo"}');
  const platform = platformName();
  assert.deepEqual(selectCodegraphIndex(root), { dirName: `.codegraph-demo-${platform}`, needsInit: true });
  write(`.codegraph-old-project-${platform}/codegraph.db`);
  write(`.codegraph-other-${platform === 'windows' ? 'posix' : 'windows'}/codegraph.db`);
  assert.deepEqual(selectCodegraphIndex(root), { dirName: `.codegraph-old-project-${platform}`, needsInit: false });
  write(`.codegraph-second-${platform}/codegraph.db`);
  assert.throws(() => selectCodegraphIndex(root), /multiple CodeGraph indexes/);
  assert.equal(fs.existsSync(path.join(root, `.codegraph-demo-${platform}`)), false);
});
