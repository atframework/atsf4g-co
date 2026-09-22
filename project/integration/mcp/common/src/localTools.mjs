/** Discover existing tools without shell shims, downloads or workspace mutation. */
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

export const CODEGRAPH_NODE_FLAGS = ['--liftoff-only', '--disable-warning=ExperimentalWarning'];
export const CODEGRAPH_LOCAL_ENV = {
  CODEGRAPH_NO_DAEMON: '1', DO_NOT_TRACK: '1', CODEGRAPH_TELEMETRY: '0',
  CODEGRAPH_NO_UPDATE_CHECK: '1', CODEGRAPH_NO_DOWNLOAD: '1',
};

export function isFile(file) {
  try { return fs.statSync(file).isFile(); } catch { return false; }
}

export function readJson(file) {
  try { return JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, '')); } catch { return null; }
}

export function commandPaths(name, env = process.env) {
  const key = Object.keys(env).find(key => key.toLowerCase() === 'path');
  const names = process.platform === 'win32' ? [name + '.exe', name, name + '.cmd', name + '.ps1'] : [name];
  return [...new Set((env[key] ?? '').split(path.delimiter).filter(Boolean).flatMap(directory => names.map(file => path.join(directory.replace(/^"|"$/g, ''), file))).filter(isFile))];
}

export function sameVersion(text, version) {
  return new RegExp(`(?:^|[\\sv])${version.replaceAll('.', '\\.')}($|\\s)`).test(text.trim());
}

export function probeTgrep(binary, version, execute) {
  if (!isFile(binary) || /\.(?:cmd|bat|ps1)$/i.test(binary)) throw new Error('tgrep requires an executable file, not a shell shim');
  const result = execute([binary, '--version'], { timeout: 15000 });
  if (result.status !== 0 || !sameVersion(result.stdout, version)) throw new Error(`tgrep must report version ${version}`);
  const help = execute([binary, 'serve', '--help'], { timeout: 15000 });
  if (help.status !== 0 || !/--transport/.test(help.stdout) || !/\bstdio\b/.test(help.stdout)) throw new Error('tgrep requires the integration stdio transport patch');
  return result.stdout.trim();
}

/** Candidate can be a bundle, compiled source/npm package, or its CLI launcher. */
export function codegraphLayouts(candidate, target) {
  if (!candidate || !fs.existsSync(candidate)) return [];
  let directory = fs.statSync(candidate).isDirectory() ? candidate : path.dirname(candidate);
  const roots = new Set();
  for (let depth = 0; depth < 5; depth++) {
    roots.add(directory);
    roots.add(path.join(directory, 'node_modules', '@colbymchenry', `codegraph-${target}`));
    roots.add(path.join(directory, '..', `codegraph-${target}`));
    const parent = path.dirname(directory);
    if (parent === directory) break;
    directory = parent;
  }
  const layouts = [];
  for (const root of roots) {
    const metadata = readJson(path.join(root, 'package.json'));
    const bundledMetadata = readJson(path.join(root, 'lib/package.json'));
    if (bundledMetadata?.name === '@colbymchenry/codegraph') {
      const runtime = path.join(root, process.platform === 'win32' ? 'node.exe' : 'node');
      if (isFile(runtime)) layouts.push({ version: bundledMetadata.version, runtime, cli_entry: path.join(root, 'lib/dist/bin/codegraph.js'), library_entry: path.join(root, 'lib/dist/index.js'), package_root: root });
    }
    if (metadata?.name === '@colbymchenry/codegraph' && isFile(path.join(root, 'dist/index.js'))) {
      layouts.push({ version: metadata.version, runtime: process.execPath, cli_entry: path.join(root, 'dist/bin/codegraph.js'), library_entry: path.join(root, 'dist/index.js'), package_root: root });
    }
  }
  return layouts;
}

export function probeCodegraph(layout, version, execute) {
  if (layout.version !== version) throw new Error(`CodeGraph ${layout.version} is not the supported ${version}`);
  for (const key of ['runtime', 'cli_entry', 'library_entry']) if (!isFile(layout[key])) throw new Error(`CodeGraph is missing ${key}: ${layout[key]}`);
  const env = { ...process.env, ...CODEGRAPH_LOCAL_ENV };
  delete env.NODE_OPTIONS;
  delete env.NODE_PATH;
  delete env.CODEGRAPH_HOST_PPID;
  delete env.CODEGRAPH_DAEMON_INTERNAL;
  const node = execute([layout.runtime, '--version'], { env, timeout: 15000 });
  const match = node.stdout?.trim().match(/^v(\d+)\.(\d+)\./);
  if (node.status !== 0 || !match || Number(match[1]) < 22 || (Number(match[1]) === 22 && Number(match[2]) < 5)) throw new Error('CodeGraph needs a local Node runtime with node:sqlite (22.5+)');
  const cli = execute([layout.runtime, ...CODEGRAPH_NODE_FLAGS, layout.cli_entry, '--version'], { env, timeout: 15000 });
  if (cli.status !== 0 || !sameVersion(cli.stdout, version)) throw new Error(`CodeGraph CLI must report version ${version}`);
  const script = 'await import("node:sqlite"); const m = await import(process.argv[1]); const C = m.CodeGraph; if (![C?.init, C?.open, C?.prototype?.indexAll, C?.prototype?.sync, C?.prototype?.watch, C?.prototype?.close].every(f => typeof f === "function")) throw new Error("incompatible CodeGraph library"); console.log("MCP_CODEGRAPH_LIBRARY_OK");';
  const library = execute([layout.runtime, ...CODEGRAPH_NODE_FLAGS, '--input-type=module', '--eval', script, pathToFileURL(layout.library_entry).href], { env, timeout: 30000 });
  if (library.status !== 0 || !library.stdout.includes('MCP_CODEGRAPH_LIBRARY_OK')) throw new Error('CodeGraph library or its runtime dependencies are incomplete');
  return { ...layout, runtime_version: node.stdout.trim() };
}

export function npxPackageRoots(cache) {
  const directory = path.join(cache, '_npx');
  try {
    return fs.readdirSync(directory, { withFileTypes: true }).filter(entry => entry.isDirectory())
      .map(entry => path.join(directory, entry.name, 'node_modules/@colbymchenry/codegraph')).filter(root => isFile(path.join(root, 'package.json')));
  } catch { return []; }
}
