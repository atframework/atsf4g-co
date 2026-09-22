/**
 * Preparation library for the locally controlled dependencies. The user-facing
 * entry is <MCP_DIR>/setup.js; this module holds the
 * reusable steps.
 *
 * Downloads are tool dependencies only (pinned upstream sources and npm
 * packages); nothing here uploads repository source code. Verified tool paths are
 * recorded into <BUILD_DIR>/integration/mcp/state/prepared-state.json
 * and reused as-is on later runs.
 *
 * Mirror support: `npmRegistry` is passed to every npm invocation (e.g.
 * https://registry.npmmirror.com); `cargoConfigArgs` are appended to the
 * tgrep cargo build (e.g. rsproxy source replacement via `--config`), so no
 * global npm/cargo configuration is mutated.
 */

import { spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { commandPaths, nodeExecutables, readJson, probeTgrep, codegraphLayouts, probeCodegraph, npxPackageRoots, sameVersion, CODEGRAPH_LOCAL_ENV } from './localTools.mjs';

import { WorkspacePaths, INTEGRATION_ROOT, validateWorkspaceBuildDir, isWithin } from './paths.mjs';
import { prepareSirchmunk } from '../../tools/sirchmunk/src/prepare.mjs';
import { run } from './command.mjs';
export { run } from './command.mjs';

export function makeLogger(write) {
  return (message) => write(`prepare: ${message}\n`);
}

const silentLog = () => {};

export function shaOf(filePath, algorithm = 'sha256') {
  return createHash(algorithm).update(fs.readFileSync(filePath)).digest('hex');
}

// -- npm ----------------------------------------------------------------------

/**
 * Prefer driving npm through its CLI JS on Node (even under Bun/Deno): no shell,
 * no .cmd resolution (Windows blocks bare .cmd spawns without a shell).
 */
export function npmInvocation() {
  for (const executable of nodeExecutables()) {
    const execDir = path.dirname(executable);
    const candidates = [
      path.join(execDir, 'node_modules', 'npm', 'bin', 'npm-cli.js'),
      path.join(execDir, '..', 'lib', 'node_modules', 'npm', 'bin', 'npm-cli.js'),
      ...commandPaths('npm').filter(file => !/\.(?:cmd|bat|ps1)$/i.test(file)).map(file => fs.realpathSync(file)),
    ];
    for (const candidate of candidates) {
      if (path.basename(candidate) === 'npm-cli.js' && fs.existsSync(candidate)) {
        return { argv: [executable, candidate], shell: false };
      }
    }
  }
  return { argv: [process.platform === 'win32' ? 'npm.cmd' : 'npm'], shell: process.platform === 'win32' };
}

export function npmCommand(extraArgs = [], { registry = null } = {}) {
  const { argv, shell } = npmInvocation();
  const args = [...argv.slice(1), ...extraArgs];
  if (registry) {
    args.push('--registry', registry);
  }
  return { command: argv[0], args, shell };
}

export function npmInstall(pkgDir, { offline = false, registry = null, cache = null, force = false, log = silentLog } = {}) {
  if (!force && fs.existsSync(path.join(pkgDir, 'node_modules'))) {
    return false;
  }
  const verb = fs.existsSync(path.join(pkgDir, 'package-lock.json')) ? 'ci' : 'install';
  const extra = offline ? ['--offline', '--no-audit', '--no-fund'] : ['--no-audit', '--no-fund'];
  if (cache) extra.push('--cache', cache);
  const { command, args, shell } = npmCommand([verb, ...extra], { registry });
  log(`npm ${verb} in ${path.basename(pkgDir)}${registry ? ' (mirror)' : ''}`);
  const result = spawnSync(command, args, {
    cwd: pkgDir,
    shell,
    stdio: 'inherit',
    env: process.env,
    windowsHide: true,
  });
  if (result.status !== 0) {
    throw new Error(`npm ${verb} failed in ${pkgDir} (exit ${result.status})`);
  }
  return true;
}

/** Deno's Windows cpSync applies chmod to long paths, which can fail after copy.
 * Copy files directly; preserve modes only on platforms that use executable bits.
 * A development symlink is reusable only if it resolves inside this dependency tree.
 */
export function copyNodeModules(source, destination, root = fs.realpathSync(source), ancestors = new Set()) {
  const actual = fs.realpathSync(source);
  if (!isWithin(actual, root)) throw new Error('cached npm dependency links outside its package tree');
  const stat = fs.statSync(actual);
  if (stat.isDirectory()) {
    if (ancestors.has(actual)) throw new Error('cached npm dependency has a directory symlink cycle');
    const next = new Set([...ancestors, actual]);
    fs.mkdirSync(destination, { recursive: true });
    for (const item of fs.readdirSync(actual)) {
      if (item !== '.bin') copyNodeModules(path.join(actual, item), path.join(destination, item), root, next);
    }
  } else if (stat.isFile()) {
    fs.copyFileSync(actual, destination);
    if (process.platform !== 'win32') fs.chmodSync(destination, stat.mode);
  }
}

export function prepareNodeModules(mcpRoot, { paths = null, offline = false, registry = null, only = null, install = npmInstall, log = silentLog } = {}) {
  const dirs = only ?? ['common', 'tools/tgrep', 'tools/codegraph'];
  const installed = [];
  for (const dir of dirs) {
    const source = path.join(mcpRoot, dir);
    const target = paths ? paths.nodePackageDir(dir) : source;
    let changed = false;
    if (paths) {
      validateWorkspaceBuildDir(paths.repoRoot, target);
      fs.mkdirSync(target, { recursive: true });
      for (const name of ['package.json', 'package-lock.json']) {
        const original = path.join(source, name);
        const copy = path.join(target, name);
        if (!fs.existsSync(original)) continue;
        const content = fs.readFileSync(original);
        if (!fs.existsSync(copy) || !fs.readFileSync(copy).equals(content)) {
          fs.writeFileSync(copy, content);
          changed = true;
        }
      }
    }
    const digest = createHash('sha256');
    for (const name of ['package.json', 'package-lock.json']) {
      const file = path.join(target, name);
      if (fs.existsSync(file)) digest.update(fs.readFileSync(file));
    }
    const fingerprint = digest.digest('hex');
    const receipt = path.join(target, '.prepared-manifest');
    const verified = fs.existsSync(receipt) && fs.readFileSync(receipt, 'utf8') === fingerprint;
    let reused = false;
    if (paths && !verified) {
      const lock = readJson(path.join(source, 'package-lock.json'));
      const packages = Object.entries(lock?.packages ?? {}).filter(([name]) => name.startsWith('node_modules/'));
      const candidates = [source, ...(dir.startsWith('tools/') ? [path.join(mcpRoot, dir.slice(6))] : [])];
      const cached = candidates.find(directory => packages.length && packages.every(([name, info]) => readJson(path.join(directory, name, 'package.json'))?.version === info.version));
      if (cached) {
        log(`reusing installed ${dir} dependencies in the workspace download directory`);
        copyNodeModules(path.join(cached, 'node_modules'), path.join(target, 'node_modules'));
        reused = true;
      }
    }
    if (install(target, { offline, registry, cache: paths && path.join(paths.cacheDir, 'npm'), force: paths ? !reused && (changed || !verified) : false, log })) {
      installed.push(dir);
    }
    if (paths && (!verified || changed)) fs.writeFileSync(receipt, fingerprint);
  }
  return { installed };
}

// -- tgrep --------------------------------------------------------------------

/** Official Cargo must not inherit a user's crates-io replacement. Cargo cannot
 * override replace-with back to crates-io (that creates a source cycle). Use a
 * workspace cache and a neutral cwd; --manifest-path keeps all build output local.
 */
export function runCargoBuild(paths, srcDir, command, { cargoConfigArgs = [], cargoIsolated = false, execute = run } = {}) {
  if (!cargoIsolated) {
    const cargoHome = path.join(paths.cacheDir, 'cargo');
    validateWorkspaceBuildDir(paths.repoRoot, cargoHome);
    fs.mkdirSync(cargoHome, { recursive: true });
    return execute([...command, ...cargoConfigArgs], { cwd: srcDir, env: { ...process.env, CARGO_HOME: cargoHome, CARGO_TARGET_DIR: path.join(srcDir, 'target') } });
  }
  const cargoHome = path.join(paths.cacheDir, 'cargo-official');
  validateWorkspaceBuildDir(paths.repoRoot, cargoHome);
  const cwd = path.parse(path.resolve(srcDir)).root;
  for (const directory of [cargoHome, path.join(cwd, '.cargo')]) {
    for (const name of ['config', 'config.toml']) {
      const config = path.join(directory, name);
      if (fs.existsSync(config)) throw new Error(`official Cargo source would inherit ${config}; resolve this config before preparing dependencies`);
    }
  }
  fs.mkdirSync(cargoHome, { recursive: true });
  return execute([...command, '--manifest-path', path.join(srcDir, 'Cargo.toml'), ...cargoConfigArgs], {
    cwd,
    env: { ...process.env, CARGO_HOME: cargoHome, CARGO_REGISTRIES_CRATES_IO_PROTOCOL: 'sparse',
      CARGO_REGISTRIES_CRATES_IO_INDEX: 'sparse+https://index.crates.io/', CARGO_TARGET_DIR: path.join(srcDir, 'target') },
  });
}

export function prepareTgrep(paths, mcpRoot, { offline = false, cargoConfigArgs = [], cargoIsolated = false, binary = null, execute = run, log = silentLog } = {}) {
  const lock = JSON.parse(fs.readFileSync(path.join(mcpRoot, 'tools/tgrep', 'upstream-lock.json'), 'utf8'));
  const legacySource = path.join(paths.integrationDir, 'upstream/tgrep-src');
  const srcDir = fs.existsSync(legacySource) ? legacySource : path.join(paths.upstreamDir, 'tgrep-src');
  const patchPath = path.join(mcpRoot, 'tools/tgrep', lock.patches[0]);
  const binaryName = process.platform === 'win32' ? 'tgrep.exe' : 'tgrep';
  const runtimeBinary = path.join(paths.runtimeDir, binaryName);

  const prepared = readJson(paths.preparedStatePath());
  const candidates = binary ? [path.resolve(binary)] : [
    prepared?.tgrep?.binary, ...commandPaths('tgrep'), runtimeBinary,
    path.join(paths.integrationDir, 'runtime', binaryName),
    path.join(srcDir, 'target/release', binaryName),
  ];
  for (const candidate of new Set(candidates.filter(Boolean))) {
    if (!binary && !fs.existsSync(candidate)) continue;
    try {
      const version = probeTgrep(candidate, lock.declared_version, execute);
      log('reusing local tgrep: ' + candidate);
      return { binary: fs.realpathSync(candidate), version, sha256: shaOf(candidate), acquisition: 'local' };
    } catch (error) {
      if (binary) throw error;
      log('skipping incompatible local tgrep: ' + candidate + ' (' + error.message + ')');
    }
  }

  if (!fs.existsSync(srcDir)) {
    if (offline) {
      throw new Error('tgrep source missing and --offline given');
    }
    log(`cloning tgrep at ${lock.source_commit.slice(0, 12)}`);
    fs.mkdirSync(path.dirname(srcDir), { recursive: true });
    run(['git', 'init', srcDir]);
    run(['git', '-C', srcDir, 'remote', 'add', 'origin', lock.repo_url]);
    run(['git', '-C', srcDir, 'fetch', '--quiet', '--depth', '1', 'origin', lock.source_commit]);
    run(['git', '-C', srcDir, 'checkout', '--quiet', 'FETCH_HEAD']);
  }
  const head = run(['git', '-C', srcDir, 'rev-parse', 'HEAD']).stdout.trim();
  if (head !== lock.source_commit) {
    throw new Error(`tgrep checkout is at ${head}, expected ${lock.source_commit}; delete upstream/tgrep-src to re-pin`);
  }

  const status = run(['git', '-C', srcDir, 'status', '--porcelain'], { check: false }).stdout.trim();
  if (!status) {
    log('applying integration patch');
    run(['git', '-C', srcDir, 'apply', '--check', patchPath]);
    run(['git', '-C', srcDir, 'apply', patchPath]);
  } else {
    log('tgrep worktree already carries the integration patch');
  }

  const buildOutput = path.join(srcDir, 'target', 'release', binaryName);
  if (!fs.existsSync(buildOutput)) {
    if (offline) {
      throw new Error('tgrep build output missing and --offline given');
    }
    log('building tgrep (cargo release); this can take several minutes');
    runCargoBuild(paths, srcDir, lock.build_command, { cargoConfigArgs, cargoIsolated });
  }
  if (!fs.existsSync(buildOutput)) {
    throw new Error('cargo build finished without producing the binary');
  }

  fs.mkdirSync(paths.runtimeDir, { recursive: true });
  fs.copyFileSync(buildOutput, runtimeBinary);
  const version = probeTgrep(runtimeBinary, lock.declared_version, execute);
  return {
    binary: runtimeBinary,
    acquisition: 'build',
    sha256: shaOf(runtimeBinary),
    version,
    source_commit: head,
    patch: path.basename(patchPath),
    patch_sha256: shaOf(patchPath),
  };
}

// -- codegraph ----------------------------------------------------------------

export function platformTarget() {
  const platform = process.platform;
  let arch = process.arch;
  if (platform === 'win32' && process.env.PROCESSOR_ARCHITECTURE === 'ARM64') {
    arch = 'arm64';
  }
  return `${platform}-${arch}`;
}

/** Reuse local code/library layouts, then warm and verify the official npx package. */
export function prepareCodegraph(paths, mcpRoot, {
  offline = false, registry = null, localPath = null, execute = run, log = silentLog,
} = {}) {
  const lock = readJson(path.join(mcpRoot, 'tools/codegraph/upstream-lock.json'));
  const target = platformTarget();
  const prepared = readJson(paths.preparedStatePath());
  const checked = new Set();
  function select(candidates) {
    for (const candidate of candidates.filter(Boolean)) {
      for (const layout of codegraphLayouts(candidate, target)) {
        const key = JSON.stringify([path.resolve(layout.library_entry), path.resolve(layout.runtime)]);
        if (checked.has(key)) continue;
        checked.add(key);
        try { return probeCodegraph(layout, lock.version, execute); }
        catch (error) { log('skipping local CodeGraph: ' + layout.package_root + ' (' + error.message + ')'); }
      }
    }
    return null;
  }
  function state(layout, acquisition, extra = {}) {
    return { ...layout, platform_target: target, acquisition, ...extra };
  }
  if (localPath) {
    const selected = select([path.resolve(localPath)]);
    if (!selected) throw new Error('explicit CodeGraph path has no compatible compiled ' + lock.version + ' library/runtime; use a prepared source checkout, npm installation or bundle');
    log('reusing local CodeGraph: ' + selected.package_root);
    return state(selected, 'local');
  }

  let selected = select([
    prepared?.codegraph?.package_root, prepared?.codegraph?.cli_entry,
    ...commandPaths('codegraph'),
    path.join(mcpRoot, 'node_modules/@colbymchenry/codegraph'),
    path.join(paths.repoRoot, 'node_modules/@colbymchenry/codegraph'),
    path.join(paths.upstreamDir, 'codegraph-bundle'),
    path.join(paths.integrationDir, 'upstream/codegraph-bundle'),
    path.join(process.env.CODEGRAPH_INSTALL_DIR || path.join(os.homedir(), '.codegraph'), 'bundles', target + '-' + lock.version),
  ]);
  if (selected) { log('reusing local CodeGraph: ' + selected.package_root); return state(selected, 'local'); }

  function npmRead(args) {
    const spec = npmCommand(args);
    const result = execute([spec.command, ...spec.args], { check: false, timeout: 15000 });
    return result.status === 0 ? result.stdout.trim().split(/\r?\n/).at(-1) : null;
  }
  const globalRoot = npmRead(['root', '--global']);
  const userCache = npmRead(['config', 'get', 'cache']);
  const cache = path.join(paths.cacheDir, 'npm', target);
  selected = select([
    globalRoot && path.join(globalRoot, '@colbymchenry/codegraph'),
    ...(userCache ? npxPackageRoots(userCache) : []),
    ...npxPackageRoots(cache),
    ...npxPackageRoots(path.join(paths.integrationDir, 'npm-cache', target)),
  ]);
  if (selected) { log('reusing local CodeGraph: ' + selected.package_root); return state(selected, 'local'); }

  const cached = prepareCodegraphNpxCache(paths, lock, { offline, registry, execute, log });
  checked.clear(); // Re-check a previously incomplete cache after npm preparation.
  selected = select(npxPackageRoots(cached.npm_cache));
  if (!selected) throw new Error('npx returned successfully but its cached CodeGraph library/runtime is missing or incompatible');
  log('npx cache verified offline: ' + selected.package_root);
  return state(selected, 'npx', cached);
}

/** Equivalent to npx --yes @colbymchenry/codegraph@<pin> --version, then offline verification. */
export function prepareCodegraphNpxCache(paths, lock, { offline = false, registry = null, execute = run, log = silentLog } = {}) {
  const target = platformTarget();
  const cache = path.join(paths.cacheDir, 'npm', target);
  const cwd = path.join(paths.agentTmpDir, 'npm-run');
  validateWorkspaceBuildDir(paths.repoRoot, cache);
  validateWorkspaceBuildDir(paths.repoRoot, cwd);
  const packageSpec = lock.npm_package + '@' + lock.version;
  const env = { ...process.env, ...CODEGRAPH_LOCAL_ENV };
  delete env.NODE_OPTIONS;
  delete env.NODE_PATH;
  delete env.CODEGRAPH_HOST_PPID;
  delete env.CODEGRAPH_DAEMON_INTERNAL;
  fs.mkdirSync(cwd, { recursive: true });
  function invokeNpx(onlyCache) {
    const args = ['exec', '--yes', '--ignore-scripts', '--include=optional', '--no-audit', '--no-fund', '--cache', cache,
      '--package=' + packageSpec, ...(onlyCache ? ['--offline'] : ['--prefer-offline'])];
    const command = npmCommand(args, { registry: registry ?? 'https://registry.npmjs.org' });
    command.args.push('--', 'codegraph', '--version');
    // npm owns package acquisition. Disable the npm shim's independent binary
    // download, so a mirror missing optional dependencies fails before config writes.
    const result = execute([command.command, ...command.args], { cwd, env, check: false, timeout: onlyCache ? 60000 : 600000 });
    if (result.status !== 0 || !sameVersion(result.stdout ?? '', lock.version)) {
      throw new Error('npx CodeGraph ' + (onlyCache ? 'offline cache verification' : 'cache preparation') +
        ' failed; confirm the chosen registry provides ' + lock.npm_platform_prefix + '-' + target +
        '@' + lock.version + ', or choose --npm-mirror=official. The GitHub binary fallback is disabled.\n' +
        String(result.stderr ?? result.error?.message ?? '').slice(-2000));
    }
  }
  log((offline ? 'checking cached ' : 'preparing npx cache for ') + packageSpec);
  invokeNpx(offline);
  if (!offline) invokeNpx(true);
  return { npm_package: packageSpec, npm_cache: cache, npm_registry: registry ?? 'https://registry.npmjs.org', offline_verified: true };
}

// -- orchestration ------------------------------------------------------------

/**
 * Run the preparation for the selected backend(s). Returns the prepared-state
 * payload; the caller (setup.js) serializes it. Throws on any failure --
 * callers must not touch agent configuration when this fails.
 */
export function runPrepare({ repoRoot, buildDir, backend, integrationRoot = null, offline = false, npmRegistry = null, cargoConfigArgs = [], cargoIsolated = false, tgrepBinary = null, codegraphPath = null, sirchmunkPython = null, pipIndexURL = 'https://pypi.org/simple', log = silentLog }) {
  const mcpRoot = integrationRoot ?? INTEGRATION_ROOT;
  const paths = new WorkspacePaths(repoRoot, buildDir);
  paths.ensureDirs();

  const state = {
    prepared_unix: Date.now(),
    platform: process.platform,
    node: process.version,
    repo_root: repoRoot,
    backend,
  };

  const nodeDirs = backend === 'codegraph' ? ['common', 'tools/codegraph'] : ['common'];
  log('installing npm dependencies');
  state.npm = prepareNodeModules(mcpRoot, { paths, offline, registry: npmRegistry, only: nodeDirs, log });

  if (backend === 'tgrep') {
    state.tgrep = prepareTgrep(paths, mcpRoot, { offline, cargoConfigArgs, cargoIsolated, binary: tgrepBinary, log });
    log(`tgrep ready: ${state.tgrep.version}`);
  } else if (backend === 'sirchmunk') {
    state.sirchmunk = prepareSirchmunk(paths, mcpRoot, { offline, python: sirchmunkPython, indexURL: pipIndexURL, log });
    log(`Sirchmunk ready: ${state.sirchmunk.version}`);
  } else {
    state.codegraph = prepareCodegraph(paths, mcpRoot, { offline, registry: npmRegistry, localPath: codegraphPath, log });
    log(`codegraph ready: ${state.codegraph.version} on ${state.codegraph.platform_target} (${state.codegraph.runtime_version})`);
  }
  return { paths, state };
}

export function writePreparedState(paths, state) {
  const outPath = paths.preparedStatePath();
  fs.writeFileSync(outPath, `${JSON.stringify(state, null, 2)}\n`, 'utf8');
  return outPath;
}
