/**
 * Preparation library for the locally controlled dependencies. The user-facing
 * entry is <repo>/project/integration/mcp/setup.js; this module holds the
 * reusable steps.
 *
 * Downloads are tool dependencies only (pinned upstream sources and npm
 * packages); nothing here uploads repository source code. Every artifact is
 * hash-recorded into <BUILD_DIR>/integration/mcp/state/prepared-state.json
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

import { WorkspacePaths } from './paths.mjs';

export function makeLogger(write) {
  return (message) => write(`prepare: ${message}\n`);
}

const silentLog = () => {};

/** Run a command with an argument array, shell disabled. */
export function run(argv, { cwd = null, env = null, check = true } = {}) {
  const result = spawnSync(argv[0], argv.slice(1), {
    cwd,
    env,
    shell: false,
    encoding: 'utf8',
    windowsHide: true,
  });
  if (check && result.status !== 0) {
    const out = `${result.stdout ?? ''}`.slice(-2000);
    const err = `${result.stderr ?? ''}`.slice(-2000);
    throw new Error(`command failed (${result.status}): ${argv.slice(0, 3).join(' ')}...\nstdout: ${out}\nstderr: ${err}`);
  }
  return result;
}

export function shaOf(filePath, algorithm = 'sha256') {
  return createHash(algorithm).update(fs.readFileSync(filePath)).digest('hex');
}

// -- npm ----------------------------------------------------------------------

/**
 * Prefer driving npm through its CLI JS on the current interpreter: no shell,
 * no .cmd resolution (Windows blocks bare .cmd spawns without a shell).
 */
export function npmInvocation() {
  const execDir = path.dirname(process.execPath);
  const candidates =
    process.platform === 'win32'
      ? [path.join(execDir, 'node_modules', 'npm', 'bin', 'npm-cli.js')]
      : [
          path.join(execDir, '..', 'lib', 'node_modules', 'npm', 'bin', 'npm-cli.js'),
          path.join(execDir, 'node_modules', 'npm', 'bin', 'npm-cli.js'),
        ];
  for (const candidate of candidates) {
    if (fs.existsSync(candidate)) {
      return { argv: [process.execPath, candidate], shell: false };
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

export function npmInstall(pkgDir, { offline = false, registry = null, log = silentLog } = {}) {
  if (fs.existsSync(path.join(pkgDir, 'node_modules'))) {
    return false;
  }
  const verb = fs.existsSync(path.join(pkgDir, 'package-lock.json')) ? 'ci' : 'install';
  const extra = offline ? ['--offline', '--no-audit', '--no-fund'] : ['--no-audit', '--no-fund'];
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

export function prepareNodeModules(mcpRoot, { offline = false, registry = null, only = null, log = silentLog } = {}) {
  const dirs = only ?? ['common', 'tgrep', 'codegraph'];
  const installed = [];
  for (const dir of dirs) {
    if (npmInstall(path.join(mcpRoot, dir), { offline, registry, log })) {
      installed.push(dir);
    }
  }
  return { installed };
}

// -- tgrep --------------------------------------------------------------------

export function prepareTgrep(paths, mcpRoot, { offline = false, cargoConfigArgs = [], log = silentLog } = {}) {
  const lock = JSON.parse(fs.readFileSync(path.join(mcpRoot, 'tgrep', 'upstream-lock.json'), 'utf8'));
  const srcDir = path.join(paths.upstreamDir, 'tgrep-src');
  const patchPath = path.join(mcpRoot, 'tgrep', lock.patches[0]);
  const binaryName = process.platform === 'win32' ? 'tgrep.exe' : 'tgrep';
  const runtimeBinary = path.join(paths.runtimeDir, binaryName);

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
    run([...lock.build_command, ...cargoConfigArgs], { cwd: srcDir, env: process.env });
  }
  if (!fs.existsSync(buildOutput)) {
    throw new Error('cargo build finished without producing the binary');
  }

  fs.mkdirSync(paths.runtimeDir, { recursive: true });
  fs.copyFileSync(buildOutput, runtimeBinary);
  const version = run([runtimeBinary, '--version']).stdout.trim();
  return {
    binary: runtimeBinary,
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

export function prepareCodegraph(paths, mcpRoot, { offline = false, registry = null, log = silentLog } = {}) {
  const lock = JSON.parse(fs.readFileSync(path.join(mcpRoot, 'codegraph', 'upstream-lock.json'), 'utf8'));
  const target = platformTarget();
  const expectedSha1 = lock.platform_dist_shasums?.[target] ?? null;
  const extractDir = path.join(paths.upstreamDir, 'codegraph-bundle');

  // Every platform bundle ships the same layout: a general-purpose bundled
  // Node at the root (node.exe / node) plus the app under lib/dist. Both the
  // serve child and the first-index helper run on that Node -- the library
  // needs the built-in node:sqlite module (Node 22.5+), which the wrapper must
  // not demand from the system Node.
  const entries = {
    cli: path.join(extractDir, 'lib', 'dist', 'bin', 'codegraph.js'),
    library: path.join(extractDir, 'lib', 'dist', 'index.js'),
    runtime: path.join(extractDir, process.platform === 'win32' ? 'node.exe' : 'node'),
  };

  if (!fs.existsSync(entries.cli)) {
    if (offline) {
      throw new Error('codegraph platform bundle missing and --offline given');
    }
    fs.mkdirSync(paths.upstreamDir, { recursive: true });
    const pkg = `${lock.npm_platform_prefix}-${target}@${lock.version}`;
    log(`fetching ${pkg}`);
    const { command, args, shell } = npmCommand(['pack', pkg, '--pack-destination', paths.upstreamDir], { registry });
    const result = spawnSync(command, args, {
      shell,
      cwd: paths.upstreamDir,
      stdio: 'inherit',
      env: process.env,
      windowsHide: true,
    });
    if (result.status !== 0) {
      throw new Error(`npm pack failed for ${pkg} (exit ${result.status})`);
    }
    const tarball = fs
      .readdirSync(paths.upstreamDir)
      .filter((name) => name.endsWith('.tgz') && name.includes(target))
      .map((name) => path.join(paths.upstreamDir, name))
      .sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs)[0];
    if (!tarball) {
      throw new Error('npm pack produced no platform tarball');
    }
    const actualSha1 = shaOf(tarball, 'sha1');
    if (expectedSha1) {
      if (actualSha1 !== expectedSha1) {
        throw new Error(`codegraph ${target} tarball shasum mismatch: ${actualSha1} != ${expectedSha1}`);
      }
    } else {
      log(`note: no committed shasum for ${target}; add "${target}": "${actualSha1}" to codegraph/upstream-lock.json`);
    }
    log('extracting platform bundle');
    const tempDir = fs.mkdtempSync(path.join(paths.upstreamDir, 'extract-'));
    try {
      run(['tar', '-xzf', tarball, '-C', tempDir]);
      const packageRoot = path.join(tempDir, 'package');
      if (!fs.statSync(packageRoot, { throwIfNoEntry: false })?.isDirectory()) {
        throw new Error('unexpected tarball layout: no package/ root');
      }
      fs.mkdirSync(extractDir, { recursive: true });
      fs.cpSync(packageRoot, extractDir, { recursive: true });
    } finally {
      fs.rmSync(tempDir, { recursive: true, force: true });
    }
  }

  for (const [role, entry] of Object.entries(entries)) {
    if (!fs.statSync(entry, { throwIfNoEntry: false })?.isFile()) {
      throw new Error(`codegraph bundle missing ${role} entry: ${entry}`);
    }
  }
  const nodeVersion = run([entries.runtime, '--version']).stdout.trim();
  return {
    platform_target: target,
    extract_dir: extractDir,
    cli_entry: entries.cli,
    library_entry: entries.library,
    runtime: entries.runtime,
    runtime_version: nodeVersion,
    version: lock.version,
  };
}

// -- orchestration ------------------------------------------------------------

/**
 * Run the preparation for the selected backend(s). Returns the prepared-state
 * payload; the caller (setup.js) serializes it. Throws on any failure --
 * callers must not touch agent configuration when this fails.
 */
export function runPrepare({ repoRoot, buildDir, backend, offline = false, npmRegistry = null, cargoConfigArgs = [], log = silentLog }) {
  const mcpRoot = path.join(repoRoot, 'project', 'integration', 'mcp');
  const paths = new WorkspacePaths(repoRoot, buildDir);
  paths.ensureDirs();

  const state = {
    prepared_unix: Date.now(),
    platform: process.platform,
    node: process.version,
    repo_root: repoRoot,
    backend,
  };

  const nodeDirs = backend === 'codegraph' ? ['common', 'codegraph'] : ['common', 'tgrep'];
  log('installing npm dependencies');
  state.npm = prepareNodeModules(mcpRoot, { offline, registry: npmRegistry, only: nodeDirs, log });

  if (backend === 'tgrep') {
    state.tgrep = prepareTgrep(paths, mcpRoot, { offline, cargoConfigArgs, log });
    log(`tgrep ready: ${state.tgrep.version}`);
  } else {
    state.codegraph = prepareCodegraph(paths, mcpRoot, { offline, registry: npmRegistry, log });
    log(`codegraph ready: ${state.codegraph.version} on ${state.codegraph.platform_target} (${state.codegraph.runtime_version})`);
  }
  return { paths, state };
}

export function writePreparedState(paths, state) {
  const outPath = paths.preparedStatePath();
  fs.writeFileSync(outPath, `${JSON.stringify(state, null, 2)}\n`, 'utf8');
  return outPath;
}
