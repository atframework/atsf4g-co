/**
 * Workspace path resolution and containment checks.
 *
 * - setup.js searches upward from cwd for a project root, falling back to cwd.
 *   Explicit --repo-root wins. New client entries always pin that root.
 *   Legacy wrapper launches detect the project enclosing their toolkit; only
 *   if none exists do they use the caller's workspace discovery.
 * - Containment uses canonicalized (realpath) paths and path.relative, never
 *   a string-prefix test. Windows case-insensitivity is handled by comparing
 *   normcased forms while keeping the original spelling for display.
 * - Symlinks and junctions that escape the root are rejected: realpath
 *   collapses them, so a link pointing outside fails the containment check.
 */

import { createHash } from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

export const INTEGRATION_ROOT = fileURLToPath(new URL('../../', import.meta.url));

export function canonicalize(target) {
  return fs.realpathSync(String(target));
}

export function isWithin(child, ancestor) {
  const relative = path.relative(ancestor, child);
  if (relative === '') {
    return true; // the same path
  }
  if (relative === '..' || relative.startsWith(`..${path.sep}`)) {
    return false;
  }
  return !path.isAbsolute(relative);
}

export function samePath(left, right) {
  if (process.platform === 'win32') {
    return path.normalize(left).toLowerCase() === path.normalize(right).toLowerCase();
  }
  return left === right;
}

export function validateWorkspaceRoot(root) {
  const canonical = canonicalize(root);
  const stat = fs.statSync(canonical, { throwIfNoEntry: false });
  if (!stat || !stat.isDirectory()) {
    throw new Error(`repository root is not a directory: ${canonical}`);
  }
  return canonical;
}

export function validateRepoRoot(root) {
  return validateWorkspaceRoot(root);
}

function readText(file) {
  try { return fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''); }
  catch (error) { if (['ENOENT', 'EISDIR', 'ENOTDIR'].includes(error.code)) return ''; throw error; }
}

function directoryNames(root) {
  try { return fs.readdirSync(root).sort(); }
  catch (error) { if (['ENOENT', 'ENOTDIR'].includes(error.code)) return []; throw error; }
}

function packageMetadata(root) {
  if (isWithin(root, INTEGRATION_ROOT)) return null;
  try { const data = JSON.parse(readText(path.join(root, 'package.json'))); return data && typeof data === 'object' && !Array.isArray(data) ? data : null; }
  catch (error) { if (error instanceof SyntaxError) return null; throw error; }
}

function cmakeProjectName(root) {
  const text = readText(path.join(root, 'CMakeLists.txt')).replace(/#\[(=*)\[[\s\S]*?\]\1\]/g, '').replace(/#[^\r\n]*/g, '');
  const name = text.match(/(?:^|\n)\s*project\s*\(\s*(?:"([^"$]+)"|([A-Za-z0-9_.+-]+))/i);
  return name?.[1] ?? name?.[2] ?? null;
}

function tomlProjectName(text, sections) {
  let section = '';
  let multiline = null;
  for (const line of text.split(/\r?\n/)) {
    if (multiline) { if (line.includes(multiline)) multiline = null; continue; }
    if (/^\s*(?:#|$)/.test(line)) continue;
    const header = line.match(/^\s*(?:\[([^\]]+)\]|\[\[([^\]]+)\]\])\s*(?:#.*)?$/);
    if (header) { section = header[1] ?? ''; continue; }
    const quoted = line.match(/=\s*("""|''')/);
    if (quoted) { if (!line.slice(line.indexOf(quoted[1]) + 3).includes(quoted[1])) multiline = quoted[1]; continue; }
    if (!sections.includes(section)) continue;
    const name = line.match(/^\s*name\s*=\s*(?:"([^"\\]*)"|'([^']*)')\s*(?:#.*)?$/);
    if (name) return name[1] ?? name[2];
  }
  return null;
}

/** Local discovery only: never run a project script or contact a VCS server. */
export function detectWorkspace(cwd = process.cwd(), { explicit, stopAt } = {}) {
  const start = validateWorkspaceRoot(explicit ?? cwd);
  if (explicit) return { root: start, detected: false, reason: 'explicit' };
  let root = start;
  let perforce = null;
  for (;;) {
    const names = directoryNames(root);
    const project = names.find(name => /\.(?:uproject|sln|slnx)$/i.test(name));
    const vcs = ['.git', '.hg', '.svn', '.p4config'].find(name => names.includes(name));
    const manifest = ['Cargo.toml', 'go.work', 'go.mod', 'pyproject.toml'].find(name => names.includes(name));
    const reason = project ?? vcs ?? manifest ?? (packageMetadata(root) ? 'package.json' : null) ?? (cmakeProjectName(root) ? 'CMakeLists.txt' : null);
    if (reason) return { root, detected: true, reason };
    if (!perforce && names.includes('.p4ignore') && !isWithin(root, INTEGRATION_ROOT)) perforce = { root, detected: true, reason: '.p4ignore' };
    const parent = path.dirname(root);
    if (samePath(parent, root) || (stopAt && samePath(root, stopAt))) break;
    root = parent;
  }
  return perforce ?? { root: start, detected: false, reason: 'cwd' };
}

export function projectInfo(root) {
  const names = directoryNames(root);
  const unreal = names.filter(name => /\.uproject$/i.test(name));
  const solutions = [...new Set(names.filter(name => /\.(?:sln|slnx)$/i.test(name)).map(name => name.replace(/\.(?:sln|slnx)$/i, '')))];
  const metadata = packageMetadata(root);
  const cmake = cmakeProjectName(root);
  const cargo = tomlProjectName(readText(path.join(root, 'Cargo.toml')), ['package']);
  const python = tomlProjectName(readText(path.join(root, 'pyproject.toml')), ['project', 'tool.poetry']);
  const go = readText(path.join(root, 'go.mod')).match(/^\s*module\s+([^\s]+)/m)?.[1]?.split('/').at(-1);
  const name = ((unreal.length === 1 ? unreal[0].slice(0, -9) : null)
    ?? (typeof metadata?.name === 'string' && metadata.name.trim() ? metadata.name.trim() : null)
    ?? cmake ?? cargo ?? python ?? go ?? (solutions.length === 1 ? solutions[0] : null) ?? path.basename(root)) || 'workspace';
  const ascii = name.normalize('NFKD').replace(/^@/, '').toLowerCase().replace(/[^a-z0-9_-]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 64);
  const slug = ascii || `workspace-${createHash('sha256').update(name).digest('hex').slice(0, 8)}`;
  const isUnreal = unreal.length > 0 || ['Engine/Source', 'Client/Engine/Source'].some(dir => fs.existsSync(path.join(root, dir)));
  return { name, slug, isUnreal };
}

export function deriveRepoRoot(entryModule, explicit) {
  if (explicit) return validateWorkspaceRoot(explicit);
  // Legacy configurations did not pass a root. Derive from their toolkit's
  // enclosing project; new configurations always pin --repo-root explicitly.
  const detected = detectWorkspace(path.dirname(fileURLToPath(entryModule)));
  return detected.detected ? detected.root : detectWorkspace().root;
}

export function defaultIndexDirName(repoRoot) {
  return `.codegraph-${projectInfo(repoRoot).slug}-${platformName()}`;
}

export function selectCodegraphIndex(repoRoot) {
  if (fs.existsSync(path.join(repoRoot, '.codegraph', 'codegraph.db'))) return { dirName: null, needsInit: false };
  const current = defaultIndexDirName(repoRoot);
  const existing = directoryNames(repoRoot).filter(name => name.startsWith('.codegraph-') && name.endsWith(`-${platformName()}`)
    && fs.existsSync(path.join(repoRoot, name, 'codegraph.db')));
  if (existing.length > 1) throw new Error('multiple CodeGraph indexes for this platform; choose one before starting MCP');
  if (existing.length === 1) return { dirName: existing[0], needsInit: false };
  return { dirName: current, needsInit: true };
}

function detectedBuildDir(repoRoot) {
  const candidates = directoryNames(repoRoot).filter(name => {
    const cache = readText(path.join(repoRoot, name, 'CMakeCache.txt'));
    const source = cache.match(/^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$/m)?.[1]?.trim();
    return source && samePath(path.resolve(source), repoRoot);
  });
  if (candidates.length === 1) return candidates[0];
  return projectInfo(repoRoot).isUnreal ? path.join('Intermediate', 'AI', 'MCP') : 'build';
}
export function resolveBuildDir(repoRoot, explicit) {
  let candidate = explicit;
  if (!candidate) {
    const settingsFile = path.join(repoRoot, '.vscode', 'settings.json');
    if (fs.existsSync(settingsFile)) {
      const jsonc = createRequire(import.meta.url)('../../agents/vendor/jsonc-parser/lib/umd/main.js');
      const errors = [];
      const settings = jsonc.parse(fs.readFileSync(settingsFile, 'utf8').replace(/^\uFEFF/, ''), errors, { allowTrailingComma: true });
      if (errors.length) throw new Error(`${settingsFile}: invalid JSON/JSONC`);
      candidate = settings?.['cmake.buildDirectory'];
      if (candidate !== undefined && (typeof candidate !== 'string' || !candidate.trim())) throw new Error('cmake.buildDirectory must be a nonempty string');
      if (!candidate) {
        const args = settings?.['clangd.arguments'];
        candidate = Array.isArray(args) ? args.find((arg) => typeof arg === 'string' && arg.startsWith('--compile-commands-dir='))?.slice('--compile-commands-dir='.length) : null;
      }
    }
    candidate = candidate || detectedBuildDir(repoRoot);
    candidate = candidate.replaceAll('${workspaceFolder}', repoRoot).replaceAll('${sourceDir}', repoRoot);
    if (candidate.includes('${')) throw new Error('unsupported build-directory variable; pass --build-dir explicitly');
  }
  if (!path.isAbsolute(candidate)) {
    candidate = path.join(repoRoot, candidate);
  }
  return path.resolve(candidate);
}

/** Reject cache paths escaping through existing junctions before prepare writes. */
export function validateWorkspaceBuildDir(repoRoot, buildDir) {
  let ancestor = path.resolve(buildDir);
  const missing = [];
  while (!fs.lstatSync(ancestor, { throwIfNoEntry: false })) {
    missing.unshift(path.basename(ancestor));
    const parent = path.dirname(ancestor);
    if (parent === ancestor) throw new Error('build directory has no existing ancestor');
    ancestor = parent;
  }
  const resolved = path.join(canonicalize(ancestor), ...missing);
  if (!isWithin(resolved, repoRoot) || samePath(resolved, repoRoot)) {
    throw new Error('构建/缓存目录必须位于当前工作区之内，且不能等于工作区根目录');
  }
  return buildDir;
}

export function workspaceId(repoRoot) {
  const key = process.platform === 'win32' ? path.normalize(repoRoot).toLowerCase() : repoRoot;
  return createHash('sha256').update(key, 'utf8').digest('hex').slice(0, 16);
}

export function platformName() {
  if (process.platform.startsWith('win')) {
    return 'windows';
  }
  if (process.platform === 'darwin') {
    return 'macos';
  }
  return 'posix';
}

/**
 * Validate a repository-relative subdirectory scope from a tool call.
 *
 * Rejects absolute paths, drive letters, UNC prefixes, backslashes, and any
 * '.'/'..' components -- the same rules the patched tgrep backend applies
 * internally. Validating here keeps error responses in the wrapper's own
 * vocabulary.
 */
export function validateRelativeScope(scope) {
  if (scope === null || scope === undefined) {
    return '';
  }
  const text = String(scope).trim();
  if (text === '') {
    return '';
  }
  if (text.startsWith('/') || text.startsWith('\\')) {
    throw new Error('scope must be a repository-relative directory');
  }
  if (text.includes('\\')) {
    throw new Error('scope must use "/" separators');
  }
  if (text.length >= 2 && text[1] === ':') {
    throw new Error('scope must not be an absolute path');
  }
  const parts = text.split('/').filter((part) => part !== '');
  for (const part of parts) {
    if (part === '.' || part === '..') {
      throw new Error("scope must not contain '.' or '..' components");
    }
  }
  return parts.join('/');
}

export class WorkspacePaths {
  constructor(repoRoot, buildDir) {
    this.repoRoot = repoRoot;
    // --build-dir is only a hint for reading caches created by older versions.
    this.explicitBuildDir = buildDir ? path.resolve(repoRoot, buildDir) : null;
    if (this.explicitBuildDir && fs.existsSync(repoRoot)) validateWorkspaceBuildDir(repoRoot, this.explicitBuildDir);
    try { this.buildDir = resolveBuildDir(repoRoot, buildDir); }
    catch { this.buildDir = path.join(repoRoot, 'build'); }
  }

  get integrationDir() {
    return path.join(this.repoRoot, '.mcp-data');
  }

  get legacyIntegrationDirs() {
    const candidates = [this.buildDir, path.join(this.repoRoot, 'Intermediate/AI/MCP'),
      ...directoryNames(this.repoRoot).map(name => path.join(this.repoRoot, name))];
    return [...new Set(candidates.map(dir => path.join(dir, 'integration', 'mcp')))].filter(dir => {
      if (!fs.existsSync(dir)) return false;
      try { validateWorkspaceBuildDir(this.repoRoot, dir); return true; } catch { return false; }
    });
  }

  /** Read-only compatibility. Never choose a legacy path as a write target. */
  readPath(relative) {
    const current = path.join(this.integrationDir, relative);
    validateWorkspaceBuildDir(this.repoRoot, current);
    if (fs.existsSync(current)) return current;
    const toolState = relative.replaceAll('\\', '/').match(/^state\/(tgrep|codegraph|sirchmunk)\/[a-f0-9]{16}\/[^/]+\/(.+)$/);
    const candidates = toolState ? this.legacyToolStateDirs(toolState[1]).map(dir => path.join(dir, toolState[2]))
      : this.legacyIntegrationDirs.map(dir => path.join(dir, relative));
    const old = candidates.filter(file => fs.existsSync(file));
    for (const file of old) validateWorkspaceBuildDir(this.repoRoot, file);
    const explicit = this.explicitBuildDir && path.join(this.explicitBuildDir, 'integration/mcp');
    const selected = explicit ? old.filter(file => isWithin(file, explicit)) : [];
    if (selected.length === 1) return selected[0];
    if (old.length > 1) throw new Error(`multiple legacy MCP caches contain ${relative}; use --build-dir to choose the read source`);
    return old[0] ?? current;
  }

  legacyToolStateDirs(tool) {
    // Older Windows versions hashed the caller's spelling, including drive
    // letter case. Inspect all old workspace IDs in this workspace's cache.
    return this.legacyIntegrationDirs.flatMap(dir => {
      const base = path.join(dir, 'state', tool);
      return directoryNames(base).filter(name => /^[a-f0-9]{16}$/.test(name)).map(name =>
        validateWorkspaceBuildDir(this.repoRoot, path.join(base, name, platformName())));
    });
  }

  get stateDir() {
    return path.join(this.integrationDir, 'state');
  }

  get runtimeDir() {
    return path.join(this.downloadsDir, 'bin', `${process.platform}-${process.arch}`);
  }

  get upstreamDir() {
    return path.join(this.downloadsDir, 'sources');
  }

  get downloadsDir() {
    return path.join(this.integrationDir, 'downloads');
  }

  get cacheDir() {
    return path.join(this.downloadsDir, 'cache');
  }

  get privateDir() {
    return path.join(this.integrationDir, 'private');
  }

  nodePackageDir(component) {
    return path.join(this.downloadsDir, 'node', `${process.platform}-${process.arch}`, component);
  }

  get agentTmpDir() {
    return path.join(this.integrationDir, 'tmp');
  }

  toolStateDir(tool) {
    return path.join(this.stateDir, tool, workspaceId(this.repoRoot), platformName());
  }

  preparedStatePath() {
    return path.join(this.stateDir, 'prepared-state.json');
  }

  preparedStateReadPath() {
    return this.readPath('state/prepared-state.json');
  }

  ensureDirs() {
    for (const dir of [this.integrationDir, this.stateDir, this.runtimeDir, this.upstreamDir, this.agentTmpDir]) {
      validateWorkspaceBuildDir(this.repoRoot, dir);
      fs.mkdirSync(dir, { recursive: true });
    }
  }
}
