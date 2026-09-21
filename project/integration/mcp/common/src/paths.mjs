/**
 * Workspace path resolution and containment checks.
 *
 * - The repository root comes from --repo-root, or is derived from the
 *   entry-point file location and validated against project markers. The
 *   wrapper never scans upwards past a validated root and never trusts the
 *   host's current working directory.
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

const MARKER_SUBPATH = path.join('project', 'integration', 'mcp');
// The installer entry is the persistent repository marker: it is the one file
// every consumer of this directory must have, and it never retires.
const MARKER_FILE = 'setup.js';
const MARKER_DISPLAY = 'project/integration/mcp/setup.js';
const DEFAULT_BUILD_DIR = 'build_jobs_cmake_tools';

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

export function validateRepoRoot(root) {
  const canonical = canonicalize(root);
  const stat = fs.statSync(canonical, { throwIfNoEntry: false });
  if (!stat || !stat.isDirectory()) {
    throw new Error(`repository root is not a directory: ${canonical}`);
  }
  const markerDir = path.join(canonical, MARKER_SUBPATH);
  if (!fs.statSync(path.join(markerDir, MARKER_FILE), { throwIfNoEntry: false })?.isFile()) {
    throw new Error(
      `repository root validation failed: ${MARKER_DISPLAY} not found; pass --repo-root explicitly`
    );
  }
  return canonical;
}

export function deriveRepoRoot(entryModule, explicit) {
  if (explicit) {
    return validateRepoRoot(explicit);
  }
  let probe = path.dirname(canonicalize(fileURLToPath(entryModule)));
  for (let depth = 0; depth < 8; depth += 1) {
    if (fs.statSync(path.join(probe, MARKER_SUBPATH, MARKER_FILE), { throwIfNoEntry: false })?.isFile()) {
      return validateRepoRoot(probe);
    }
    const parent = path.dirname(probe);
    if (parent === probe) {
      break;
    }
    probe = parent;
  }
  throw new Error('could not derive the repository root from the entry-point location; pass --repo-root explicitly');
}

export function resolveBuildDir(repoRoot, explicit) {
  let candidate = explicit;
  if (!candidate) {
    candidate = path.join(repoRoot, DEFAULT_BUILD_DIR);
  } else if (!path.isAbsolute(candidate)) {
    candidate = path.join(repoRoot, candidate);
  }
  return path.resolve(candidate);
}

export function workspaceId(repoRoot) {
  return createHash('sha256').update(repoRoot, 'utf8').digest('hex').slice(0, 16);
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
    this.buildDir = buildDir;
  }

  get integrationDir() {
    return path.join(this.buildDir, 'integration', 'mcp');
  }

  get stateDir() {
    return path.join(this.integrationDir, 'state');
  }

  get runtimeDir() {
    return path.join(this.integrationDir, 'runtime');
  }

  get upstreamDir() {
    return path.join(this.integrationDir, 'upstream');
  }

  get agentTmpDir() {
    return path.join(this.buildDir, '_agent_tmp', 'mcp');
  }

  toolStateDir(tool) {
    return path.join(this.stateDir, tool, workspaceId(this.repoRoot), platformName());
  }

  preparedStatePath() {
    return path.join(this.stateDir, 'prepared-state.json');
  }

  ensureDirs() {
    for (const dir of [this.integrationDir, this.stateDir, this.runtimeDir, this.upstreamDir, this.agentTmpDir]) {
      fs.mkdirSync(dir, { recursive: true });
    }
  }
}
