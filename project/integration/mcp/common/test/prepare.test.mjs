import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { INTEGRATION_ROOT, WorkspacePaths } from '../src/paths.mjs';
import { prepareTgrep, prepareCodegraph, prepareCodegraphNpxCache, runCargoBuild, prepareNodeModules } from '../src/prepare.mjs';

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-prepare-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const paths = new WorkspacePaths(root, path.join(root, 'build'));
  paths.ensureDirs();
  const write = (relative, text = '') => {
    const file = path.join(root, relative);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, text);
    return file;
  };
  return { root, paths, write };
}

test('moving tools preserves offline reuse of matching legacy npm dependencies', t => {
  const w = fixture(t);
  const kit = path.join(w.root, 'kit');
  w.write('kit/tools/codegraph/package.json', '{"name":"fixture","dependencies":{"fixture-dependency":"1.0.0"}}');
  w.write('kit/tools/codegraph/package-lock.json', JSON.stringify({ packages: { 'node_modules/fixture-dependency': { version: '1.0.0' } } }));
  w.write('kit/codegraph/node_modules/fixture-dependency/package.json', '{"name":"fixture-dependency","version":"1.0.0"}');
  w.write('kit/codegraph/node_modules/fixture-dependency/index.js', 'export default 42;');
  prepareNodeModules(kit, { paths: w.paths, offline: true, only: ['tools/codegraph'], install: (directory, options) => {
    assert.equal(options.force, false, 'matching legacy packages should not require an offline npm download');
    assert.equal(options.offline, true);
    assert.equal(fs.readFileSync(path.join(directory, 'node_modules/fixture-dependency/index.js'), 'utf8'), 'export default 42;');
    return false;
  } });
  assert.ok(fs.existsSync(path.join(w.paths.nodePackageDir('tools/codegraph'), '.prepared-manifest')));
});

test('a compiled local CodeGraph tries another Node runtime after rejecting an incompatible one', t => {
  const w = fixture(t);
  const libraryRoot = path.join(w.root, 'compiled-codegraph');
  w.write('compiled-codegraph/package.json', JSON.stringify({ name: '@colbymchenry/codegraph', version: '1.6.0' }));
  w.write('compiled-codegraph/dist/index.js');
  w.write('compiled-codegraph/dist/bin/codegraph.js');
  const alternate = w.write('alternate/' + (process.platform === 'win32' ? 'node.exe' : 'node'));
  const pathKey = Object.keys(process.env).find(key => key.toLowerCase() === 'path') ?? 'PATH';
  const oldPath = process.env[pathKey];
  process.env[pathKey] = path.dirname(alternate);
  t.after(() => { if (oldPath === undefined) delete process.env[pathKey]; else process.env[pathKey] = oldPath; });
  const attempted = [];
  const result = prepareCodegraph(w.paths, INTEGRATION_ROOT, { localPath: libraryRoot, offline: true, execute: argv => {
    attempted.push(argv);
    if (argv.length === 2 && argv[1] === '--version') return { status: 0, stdout: argv[0] === alternate ? 'v24.21.0\n' : 'v20.19.2\n' };
    if (argv.includes('--eval')) return { status: 0, stdout: 'MCP_CODEGRAPH_LIBRARY_OK\n' };
    return { status: 0, stdout: '1.6.0\n' };
  } });
  assert.equal(result.runtime, alternate);
  assert.equal(result.acquisition, 'local');
  assert.ok(attempted.some(argv => argv[0] !== alternate));
  assert.ok(attempted.some(argv => argv[0] === alternate && argv.includes('--eval')));
});

test('official Cargo uses a separate cache and manifest without inheriting project source replacement', t => {
  const w = fixture(t);
  const config = w.write('.cargo/config.toml', '[source.crates-io]\nreplace-with="private-mirror"\n');
  const before = fs.readFileSync(config);
  const srcDir = path.join(w.paths.upstreamDir, 'tgrep-src');
  w.write(path.relative(w.root, path.join(srcDir, 'Cargo.toml')), '[package]\nname="fixture"\nversion="1.0.0"\n');
  let calls = 0;
  runCargoBuild(w.paths, srcDir, ['cargo', 'build', '--release', '--locked'], { cargoIsolated: true, execute: (argv, options) => {
    calls++;
    assert.deepEqual(argv, ['cargo', 'build', '--release', '--locked', '--manifest-path', path.join(srcDir, 'Cargo.toml')]);
    assert.equal(options.cwd, path.parse(srcDir).root);
    assert.ok(options.env.CARGO_HOME.startsWith(w.paths.integrationDir + path.sep));
    assert.equal(options.env.CARGO_REGISTRIES_CRATES_IO_PROTOCOL, 'sparse');
    assert.equal(options.env.CARGO_REGISTRIES_CRATES_IO_INDEX, 'sparse+https://index.crates.io/');
    assert.equal(options.env.CARGO_TARGET_DIR, path.join(srcDir, 'target'));
    assert.equal(fs.existsSync(path.join(options.env.CARGO_HOME, 'config.toml')), false);
    return { status: 0 };
  } });
  assert.equal(calls, 1);
  assert.deepEqual(fs.readFileSync(config), before);
});

test('official Cargo refuses a replacement config in its isolated cache before invoking Cargo', t => {
  const w = fixture(t);
  const config = w.write(path.relative(w.root, path.join(w.paths.cacheDir, 'cargo-official/config.toml')), '[source.crates-io]\nreplace-with="other"\n');
  const before = fs.readFileSync(config);
  assert.throws(() => runCargoBuild(w.paths, path.join(w.paths.upstreamDir, 'tgrep-src'), ['cargo', 'build'], {
    cargoIsolated: true, execute: () => assert.fail('must not execute Cargo with an inherited replacement'),
  }), /official Cargo source would inherit/);
  assert.deepEqual(fs.readFileSync(config), before);
});

test('domestic Cargo source arguments reach the build without changing npm or global settings', t => {
  const w = fixture(t);
  const srcDir = path.join(w.paths.upstreamDir, 'tgrep-src');
  const args = ['--config', 'source.crates-io.replace-with="mcp-mirror"', '--config', 'source.mcp-mirror.registry="sparse+https://mirrors.nju.edu.cn/crates.io-index/"'];
  let calls = 0;
  runCargoBuild(w.paths, srcDir, ['cargo', 'build', '--locked'], { cargoConfigArgs: args, execute: (argv, options) => {
    calls++;
    assert.deepEqual(argv, ['cargo', 'build', '--locked', ...args]);
    assert.equal(options.cwd, srcDir);
    assert.equal(options.env.CARGO_HOME, path.join(w.paths.cacheDir, 'cargo'));
    assert.equal(options.env.CARGO_TARGET_DIR, path.join(srcDir, 'target'));
  } });
  assert.equal(calls, 1);
  assert.equal(fs.existsSync(path.join(w.paths.integrationDir, 'cargo-home-official')), false);
});

test('npm artifacts stay in downloads and an interrupted install cannot become a prepared cache', t => {
  const w = fixture(t);
  const kit = path.join(w.root, 'kit');
  w.write('kit/common/package.json', '{"name":"fixture","private":true}');
  const target = w.paths.nodePackageDir('common');
  const options = { paths: w.paths, only: ['common'], install: (directory, options) => {
    assert.equal(directory, target);
    assert.equal(options.force, true);
    fs.mkdirSync(path.join(directory, 'node_modules'), { recursive: true });
    throw new Error('interrupted install');
  } };
  assert.throws(() => prepareNodeModules(kit, options), /interrupted install/);
  assert.equal(fs.existsSync(path.join(kit, 'common/node_modules')), false);
  assert.equal(fs.existsSync(path.join(target, '.prepared-manifest')), false);
  let calls = 0;
  prepareNodeModules(kit, { ...options, install: (directory, settings) => { calls++; assert.equal(settings.force, true); return true; } });
  assert.equal(calls, 1);
  prepareNodeModules(kit, { ...options, install: (directory, settings) => { assert.equal(settings.force, false); return false; } });
});

test('an existing patched tgrep can be prepared offline without a source checkout or copying it', t => {
  const w = fixture(t);
  const binary = w.write('local tools/tgrep.exe', 'fixture binary');
  const calls = [];
  const execute = argv => {
    calls.push(argv);
    assert.equal(argv[0], binary);
    return { status: 0, stdout: argv.includes('--version') ? 'tgrep 1.0.9\n' : '--transport <tcp|stdio>\n', stderr: '' };
  };
  const state = prepareTgrep(w.paths, INTEGRATION_ROOT, { offline: true, binary, execute });
  assert.equal(state.binary, binary);
  assert.equal(state.acquisition, 'local');
  assert.equal(fs.existsSync(path.join(w.paths.upstreamDir, 'tgrep-src')), false);
  assert.equal(fs.existsSync(path.join(w.paths.runtimeDir, 'tgrep.exe')), false);
  assert.ok(calls.some(argv => argv.includes('--help')));
});

test('an explicit TCP-only tgrep is rejected before any source download', t => {
  const w = fixture(t);
  const binary = w.write('local/tgrep.exe', 'fixture binary');
  const execute = argv => ({ status: 0, stdout: argv.includes('--version') ? 'tgrep 1.0.9' : 'serve --port 8787', stderr: '' });
  assert.throws(() => prepareTgrep(w.paths, INTEGRATION_ROOT, { offline: true, binary, execute }), /stdio/);
  assert.equal(fs.existsSync(path.join(w.paths.upstreamDir, 'tgrep-src')), false);
});

test('a compiled CodeGraph checkout is reused without npm or a platform download', t => {
  const w = fixture(t);
  const source = path.join(w.root, 'codegraph source');
  w.write('codegraph source/package.json', '{"name":"@colbymchenry/codegraph","version":"1.6.0"}');
  w.write('codegraph source/dist/index.js');
  w.write('codegraph source/dist/bin/codegraph.js');
  const calls = [];
  const execute = argv => {
    calls.push(argv);
    if (argv.includes('--eval')) return { status: 0, stdout: 'MCP_CODEGRAPH_LIBRARY_OK\n', stderr: '' };
    return { status: 0, stdout: argv.some(arg => arg.endsWith('codegraph.js')) ? '1.6.0\n' : 'v24.0.0\n', stderr: '' };
  };
  const state = prepareCodegraph(w.paths, INTEGRATION_ROOT, { offline: true, localPath: source, execute });
  assert.equal(state.acquisition, 'local');
  assert.equal(state.library_entry, path.join(source, 'dist/index.js'));
  assert.ok(calls.some(argv => argv.includes('--eval')), 'library and sqlite must be probed');
  assert.ok(calls.every(argv => !argv.includes('pack') && !argv.includes('exec')));
});

test('npx preparation uses the selected registry and proves the cache works offline', t => {
  const w = fixture(t);
  const lock = { npm_package: '@colbymchenry/codegraph', npm_platform_prefix: '@colbymchenry/codegraph', version: '1.6.0' };
  const calls = [];
  const state = prepareCodegraphNpxCache(w.paths, lock, { registry: 'https://registry.npmmirror.com', execute: (argv, options) => {
    calls.push({ argv, options });
    return { status: 0, stdout: '1.6.0\n', stderr: '' };
  } });
  assert.equal(calls.length, 2);
  assert.ok(calls[0].argv.includes('--prefer-offline'));
  assert.ok(calls[1].argv.includes('--offline'));
  for (const { argv, options } of calls) {
    assert.ok(argv.includes('--package=@colbymchenry/codegraph@1.6.0'));
    assert.ok(argv.includes('https://registry.npmmirror.com'));
    assert.equal(options.env.CODEGRAPH_NO_DOWNLOAD, '1');
    assert.deepEqual(argv.slice(-3), ['--', 'codegraph', '--version']);
    assert.equal(argv.includes('pack'), false);
  }
  assert.equal(state.offline_verified, true);
  assert.ok(state.npm_cache.startsWith(w.paths.integrationDir));
});

test('a cache that only works online cannot be accepted as prepared', t => {
  const w = fixture(t);
  let calls = 0;
  assert.throws(() => prepareCodegraphNpxCache(w.paths, { npm_package: '@colbymchenry/codegraph', version: '1.6.0' }, { execute: argv => {
    calls++;
    return argv.includes('--offline') ? { status: 1, stdout: '', stderr: 'ENOTCACHED' } : { status: 0, stdout: '1.6.0' };
  } }), /offline cache verification/);
  assert.equal(calls, 2);
  assert.equal(fs.existsSync(w.paths.preparedStatePath()), false);
});

test('offline cache failures never try an online fallback', t => {
  const w = fixture(t);
  let calls = 0;
  assert.throws(() => prepareCodegraphNpxCache(w.paths, { npm_package: '@colbymchenry/codegraph', version: '1.6.0' }, { offline: true, execute: argv => {
    calls++;
    assert.ok(argv.includes('--offline'));
    return { status: 1, stdout: '', stderr: 'cache missing' };
  } }), /offline cache verification/);
  assert.equal(calls, 1);
});

test('a successful process reporting the wrong package version is not a valid cache', t => {
  const w = fixture(t);
  assert.throws(() => prepareCodegraphNpxCache(w.paths, { npm_package: '@colbymchenry/codegraph', version: '1.6.0' }, {
    execute: () => ({ status: 0, stdout: '2.0.0' }),
  }), /cache preparation failed/);
});
