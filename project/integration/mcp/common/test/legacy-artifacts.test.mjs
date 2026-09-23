import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { spawnSync } from 'node:child_process';
import { WorkspacePaths, INTEGRATION_ROOT } from '../src/paths.mjs';
import { prepareTgrep, prepareCodegraph } from '../src/prepare.mjs';

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'legacy-artifacts-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const paths = new WorkspacePaths(root);
  paths.ensureDirs();
  const write = (relative, data = '') => {
    const file = path.join(root, relative);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, data);
    return file;
  };
  return { root, paths, write };
}

for (const oldLayout of [false, true]) test(`offline tgrep rebuild imports legacy source and Cargo dependencies (${oldLayout ? 'original' : 'downloads'} layout)`, t => {
  const { paths, write } = fixture(t);
  const lock = JSON.parse(fs.readFileSync(path.join(INTEGRATION_ROOT, 'tools/tgrep/upstream-lock.json')));
  const base = 'old-build/integration/mcp/';
  const source = base + (oldLayout ? 'upstream/' : 'downloads/sources/') + 'tgrep-src';
  const cargo = base + (oldLayout ? 'cargo-home-official' : 'downloads/cache/cargo-official');
  const old = write(source + '/Cargo.toml', 'old source');
  write(source + '/.git/HEAD', lock.source_commit);
  const crate = write(cargo + '/registry/cache/registry/test.crate', 'cached crate');
  write(cargo + '/registry/index/registry/config.json', '{}');
  write(cargo + '/config.toml', 'must not inherit old configuration');
  const src = path.join(paths.upstreamDir, 'tgrep-src');
  let builds = 0;
  const result = prepareTgrep(paths, INTEGRATION_ROOT, { offline: true, cargoIsolated: true, execute: (argv, options) => {
    if (argv[0] === 'git') {
      assert.equal(argv[2], src, 'git must only mutate the imported source');
      if (argv.includes('rev-parse')) return { status: 0, stdout: lock.source_commit };
      assert.ok(argv.includes('apply'), 'no clone/fetch on offline migration');
      return { status: argv.includes('--reverse') ? 1 : 0 };
    }
    if (argv[0] === 'cargo') {
      builds++;
      assert.ok(argv.includes('--offline') && argv.includes('--locked'));
      assert.equal(options.env.CARGO_HOME, path.join(paths.cacheDir, 'cargo-official'));
      assert.equal(fs.readFileSync(path.join(options.env.CARGO_HOME, 'registry/cache/registry/test.crate'), 'utf8'), 'cached crate');
      assert.equal(fs.existsSync(path.join(options.env.CARGO_HOME, 'config.toml')), false);
      assert.equal(fs.readFileSync(path.join(src, 'Cargo.toml'), 'utf8'), 'old source');
      write(path.relative(paths.repoRoot, path.join(src, 'target/release', process.platform === 'win32' ? 'tgrep.exe' : 'tgrep')), 'new binary');
      return { status: 0 };
    }
    return { status: 0, stdout: argv.includes('--version') ? 'tgrep 1.0.9 (mcp-stdio-v2)' : '--transport tcp stdio' };
  } });
  assert.equal(builds, 1);
  assert.equal(result.acquisition, 'build');
  assert.equal(fs.readFileSync(old, 'utf8'), 'old source');
  assert.equal(fs.readFileSync(crate, 'utf8'), 'cached crate');
  assert.equal(fs.existsSync(path.join(paths.repoRoot, source, 'target')), false);
});

for (const relative of ['runtime', `downloads/bin/${process.platform}-${process.arch}`, 'upstream/tgrep-src/target/release']) {
  test(`compatible legacy tgrep is reused from ${relative}`, t => {
    const { paths, write } = fixture(t);
    const binary = write('old-build/integration/mcp/' + relative + '/' + (process.platform === 'win32' ? 'tgrep.exe' : 'tgrep'), 'verified binary');
    const result = prepareTgrep(paths, INTEGRATION_ROOT, { offline: true, execute: argv => {
      assert.equal(argv[0], binary);
      return { status: 0, stdout: argv.includes('--version') ? 'tgrep 1.0.9 (mcp-stdio-v2)' : '--transport tcp stdio' };
    } });
    assert.equal(result.binary, binary);
    assert.equal(result.acquisition, 'local');
    assert.equal(fs.existsSync(path.join(paths.upstreamDir, 'tgrep-src')), false);
  });
}

test('legacy artifact aliases preserve ambiguity, explicit selection and current-cache priority', t => {
  const { root, paths, write } = fixture(t);
  const first = write('old-a/integration/mcp/upstream/tgrep-src/Cargo.toml');
  write('old-b/integration/mcp/downloads/sources/tgrep-src/Cargo.toml');
  assert.throws(() => paths.readPath('downloads/sources/tgrep-src', ['upstream/tgrep-src']), /multiple legacy.*--build-dir/);
  assert.equal(new WorkspacePaths(root, 'old-a').readPath('downloads/sources/tgrep-src', ['upstream/tgrep-src']), path.dirname(first));
  const current = write('.mcp-data/downloads/sources/tgrep-src/Cargo.toml');
  assert.equal(paths.readPath('downloads/sources/tgrep-src', ['upstream/tgrep-src']), path.dirname(current));
});

test('an imported tgrep checkout at a different pin never gets patched or built', t => {
  const { paths, write } = fixture(t);
  const file = write('old-build/integration/mcp/upstream/tgrep-src/Cargo.toml', 'unchanged');
  assert.throws(() => prepareTgrep(paths, INTEGRATION_ROOT, { offline: true, execute: argv => {
    assert.deepEqual(argv.slice(-2), ['rev-parse', 'HEAD']);
    return { status: 0, stdout: 'wrong-commit' };
  } }), /wrong-commit, expected/);
  assert.equal(fs.readFileSync(file, 'utf8'), 'unchanged');
});

test('verified current artifacts take priority over ambiguous unrelated legacy caches', t => {
  const { paths, write } = fixture(t);
  for (const build of ['old-a', 'old-b']) {
    write(build + '/integration/mcp/upstream/tgrep-src/Cargo.toml');
    write(build + '/integration/mcp/upstream/codegraph-bundle/package.json', '{}');
  }
  const binary = write('.mcp-data/downloads/bin/current-tgrep', 'verified binary');
  const library = write('.mcp-data/downloads/sources/current-codegraph/dist/index.js');
  write('.mcp-data/downloads/sources/current-codegraph/dist/bin/codegraph.js');
  write('.mcp-data/downloads/sources/current-codegraph/package.json', '{"name":"@colbymchenry/codegraph","version":"1.6.0"}');
  write('.mcp-data/state/prepared-state.json', JSON.stringify({ tgrep: { binary }, codegraph: { package_root: path.dirname(path.dirname(library)) } }));
  assert.equal(prepareTgrep(paths, INTEGRATION_ROOT, { offline: true, execute: argv => {
    assert.equal(argv[0], binary);
    return { status: 0, stdout: argv.includes('--version') ? 'tgrep 1.0.9 (mcp-stdio-v2)' : '--transport tcp stdio' };
  } }).binary, binary);
  assert.equal(prepareCodegraph(paths, INTEGRATION_ROOT, { offline: true, execute: argv => ({ status: 0,
    stdout: argv.includes('--eval') ? 'MCP_CODEGRAPH_LIBRARY_OK' : argv.some(arg => arg.endsWith('codegraph.js')) ? '1.6.0' : 'v24.0.0' }) }).library_entry, library);
});

for (const relative of ['upstream/codegraph-bundle', 'downloads/sources/codegraph-bundle',
  `npm-cache/${process.platform}-${process.arch}/_npx/cached/node_modules/@colbymchenry/codegraph`,
  `downloads/cache/npm/${process.platform}-${process.arch}/_npx/cached/node_modules/@colbymchenry/codegraph`]) {
  test(`offline CodeGraph reuses ${relative} without prepared-state`, t => {
    const { paths, write } = fixture(t);
    const base = 'old-build/integration/mcp/' + relative;
    write(base + '/package.json', '{"name":"@colbymchenry/codegraph","version":"1.6.0"}');
    const library = write(base + '/dist/index.js');
    write(base + '/dist/bin/codegraph.js');
    const result = prepareCodegraph(paths, INTEGRATION_ROOT, { offline: true, execute: argv => {
      assert.ok(!argv.includes('exec'), 'a verified old installation needs no npm acquisition');
      if (argv.includes('--eval')) return { status: 0, stdout: 'MCP_CODEGRAPH_LIBRARY_OK' };
      if (argv.includes('root') || argv.includes('config')) return { status: 1, stdout: '' };
      return { status: 0, stdout: argv.some(arg => arg.endsWith('codegraph.js')) ? '1.6.0' : 'v24.0.0' };
    } });
    assert.equal(result.library_entry, library);
    assert.equal(result.acquisition, 'local');
  });
}

for (const tool of ['tgrep', 'codegraph', 'sirchmunk']) {
  for (const fake of [false, true]) test(`${tool}: ambiguous legacy state exits cleanly (${fake ? 'async configuration' : 'entry resolution'})`, t => {
    const { paths, write } = fixture(t);
    for (const build of ['old-a', 'old-b']) write(build + '/integration/mcp/state/prepared-state.json', '{}');
    const env = { ...process.env };
    for (const name of ['TGREP', 'CODEGRAPH', 'SIRCHMUNK']) delete env[name + '_MCP_FAKE_SCRIPT'];
    if (fake) env[tool.toUpperCase() + '_MCP_FAKE_SCRIPT'] = path.join(INTEGRATION_ROOT, 'tools', tool, 'test',
      { tgrep: 'fake-tgrep-serve.mjs', codegraph: 'fake-codegraph.mjs', sirchmunk: 'fake-backend.mjs' }[tool]);
    const result = spawnSync(process.execPath, [path.join(INTEGRATION_ROOT, 'tools', tool, 'src/server.mjs'), '--repo-root', paths.repoRoot],
      { env, encoding: 'utf8', timeout: 15000 });
    assert.equal(result.status, 2, result.stderr);
    assert.match(result.stderr, /multiple legacy MCP caches.*--build-dir/);
    assert.doesNotMatch(result.stderr, /\n\s+at |UnhandledPromiseRejection/);
    assert.equal(result.stdout, '');
  });
}
