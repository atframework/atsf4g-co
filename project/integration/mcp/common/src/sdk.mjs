/** Resolve prepared SDK packages without placing downloads inside the toolkit. */
import { createRequire } from 'node:module';
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { INTEGRATION_ROOT, WorkspacePaths, deriveRepoRoot } from './paths.mjs';

export async function loadSdk(component, name) {
  const value = flag => {
    const index = process.argv.indexOf(flag);
    return index >= 0 ? process.argv[index + 1] : process.argv.find(arg => arg.startsWith(flag + '='))?.slice(flag.length + 1);
  };
  const root = deriveRepoRoot(import.meta.url, value('--repo-root'));
  const paths = new WorkspacePaths(root, value('--build-dir'));
  const candidates = [paths.nodePackageDir(component), ...paths.legacyIntegrationDirs.map(dir => path.join(dir, 'downloads/node', `${process.platform}-${process.arch}`, component)), path.join(INTEGRATION_ROOT, component)];
  if (component.startsWith('tools/')) candidates.push(path.join(INTEGRATION_ROOT, component.slice(6)));
  const packageName = name.startsWith('@') ? name.split('/').slice(0, 2).join('/') : name.split('/')[0];
  const lock = JSON.parse(fs.readFileSync(path.join(INTEGRATION_ROOT, component, 'package-lock.json'), 'utf8'));
  const version = lock.packages?.['node_modules/' + packageName]?.version;
  for (const directory of candidates) {
    const manifest = path.join(directory, 'node_modules', packageName, 'package.json');
    if (!fs.existsSync(manifest) || JSON.parse(fs.readFileSync(manifest, 'utf8')).version !== version) continue;
    let resolved;
    try { resolved = createRequire(path.join(directory, 'package.json')).resolve(name); }
    catch (error) { if (error.code === 'MODULE_NOT_FOUND') continue; throw error; }
    return import(pathToFileURL(resolved).href);
  }
  throw new Error(`Missing prepared dependency ${name}; run setup.js in this workspace first`);
}
