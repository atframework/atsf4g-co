/** Repository-contained config writes, build-directory backups, and best-effort batch recovery. */
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { AgentConfigError } from './errors.mjs';

/** Validate both the lexical path and existing ancestors, including the leaf. */
export function assertConfigPath(repoRoot, filePath) {
  const root = path.resolve(repoRoot);
  const target = path.resolve(filePath);
  const inside = (base, candidate) => {
    const relative = path.relative(base, candidate);
    return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`));
  };
  const fail = () => { throw new AgentConfigError(`${filePath} resolves outside the repository root`, 'outside-repo'); };
  if (!inside(root, target)) fail();
  const realRoot = fs.realpathSync(root);
  let current = target;
  while (true) {
    try {
      const stat = fs.lstatSync(current);
      // A dangling symlink must not masquerade as a missing config.
      let real;
      try { real = fs.realpathSync(current); } catch { fail(); }
      if (!inside(realRoot, real)) fail();
      if (current === target && stat.isSymbolicLink()) {
        throw new AgentConfigError(`${filePath}: symbolic-link config files require manual editing`, 'symlink-target');
      }
      return;
    } catch (error) {
      if (error.code !== 'ENOENT') throw error;
      const parent = path.dirname(current);
      if (parent === current) fail();
      current = parent;
    }
  }
}

export function readConfigFile(repoRoot, filePath, encoding = 'utf8') {
  assertConfigPath(repoRoot, filePath);
  try { return fs.readFileSync(filePath, encoding); } catch (error) {
    if (error.code === 'ENOENT') return null;
    throw new AgentConfigError(`cannot read ${filePath}: ${error.message}`, 'unreadable');
  }
}

export function createFileStore({ repoRoot, stateDir, tmpDir }) {
  const backupDir = path.join(tmpDir, 'agent-config-backups');
  const stagingDir = path.join(tmpDir, 'agent-config-staging');
  const journalPath = path.join(tmpDir, 'agent-config-journal.jsonl');
  const ownershipPath = path.join(stateDir, 'agent-config-state.json');
  const mutations = new Map();
  const readFile = (file) => readConfigFile(repoRoot, file);
  const readBuffer = (file) => readConfigFile(repoRoot, file, null);
  const sameContent = (left, right) => Buffer.isBuffer(left) && Buffer.isBuffer(right) ? left.equals(right) : left === right;
  const readRecord = (record) => record.binary ? readBuffer(record.file) : readFile(record.file);
  const fileId = (file) => {
    const stat = fs.statSync(file);
    return `${stat.dev}:${stat.ino}:${stat.birthtimeMs}`;
  };

  function loadOwnership() {
    let text;
    try { text = fs.readFileSync(ownershipPath, 'utf8'); } catch (error) {
      if (error.code === 'ENOENT') return { files: {} };
      throw error;
    }
    const parsed = JSON.parse(text);
    if (!parsed || typeof parsed !== 'object' || !parsed.files || typeof parsed.files !== 'object' || Array.isArray(parsed.files)) {
      throw new AgentConfigError('invalid agent-config-state.json; restore its metadata before writing', 'invalid-state');
    }
    return parsed;
  }

  /** Staging lives under the configured build directory. EXDEV fails before replacement. */
  function atomicWrite(file, text, mode = 0o600) {
    fs.mkdirSync(stagingDir, { recursive: true });
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const temp = path.join(stagingDir, `${crypto.randomUUID()}.tmp`);
    let renamed = false;
    try {
      fs.writeFileSync(temp, text, { encoding: 'utf8', flag: 'wx', mode });
      fs.renameSync(temp, file);
      renamed = true;
    } finally {
      if (!renamed) fs.rmSync(temp, { force: true });
    }
  }

  function saveOwnership(data) {
    atomicWrite(ownershipPath, `${JSON.stringify(data, null, 2)}\n`);
  }

  function owns(relative) {
    const entry = loadOwnership().files[relative];
    const file = path.join(repoRoot, relative);
    if (entry?.created_by !== 'setup' || !entry.file_id) return false;
    if (readFile(file) === null) return false;
    return entry.file_id === fileId(file);
  }

  function journal(entry) {
    fs.mkdirSync(backupDir, { recursive: true });
    fs.appendFileSync(journalPath, `${JSON.stringify({ time: new Date().toISOString(), ...entry })}\n`, { encoding: 'utf8', mode: 0o600 });
  }

  function prepareMutation(file, after, { relative, expectedBefore }) {
    const binary = Buffer.isBuffer(after) || Buffer.isBuffer(expectedBefore);
    const before = binary ? readBuffer(file) : readFile(file);
    if (!sameContent(before, expectedBefore)) {
      throw new AgentConfigError(`${relative}: file changed concurrently since the plan was computed; aborting`, 'concurrent-modification');
    }
    if (sameContent(before, after)) return null;
    const ownership = loadOwnership().files[relative];
    const mode = before === null ? 0o600 : fs.statSync(file).mode & 0o777;
    let backupName;
    if (before !== null) {
      fs.mkdirSync(backupDir, { recursive: true });
      backupName = `${crypto.randomUUID()}.bak`;
      fs.writeFileSync(path.join(backupDir, backupName), before, { encoding: 'utf8', flag: 'wx', mode: 0o600 });
      journal({ action: 'backup', file: relative, backup: backupName });
    }
    journal({ action: 'planned', file: relative, operation: after === null ? 'delete' : before === null ? 'create' : 'update', backup: backupName });
    return { file, relative, before, after, binary, ownership, mode, wasOwned: before !== null && ownership?.created_by === 'setup' && ownership.file_id === fileId(file) };
  }

  function updateOwnership(record, restoring = false) {
    const ownership = loadOwnership();
    const { relative, file } = record;
    if (restoring) {
      if (record.ownership) {
        ownership.files[relative] = record.wasOwned ? { ...record.ownership, file_id: fileId(file) } : record.ownership;
      } else delete ownership.files[relative];
    } else if (record.after === null) {
      delete ownership.files[relative];
    } else if (record.before === null || record.wasOwned) {
      ownership.files[relative] = { created_by: 'setup', created_unix: record.ownership?.created_unix ?? Date.now(), file_id: fileId(file) };
    }
    saveOwnership(ownership);
  }

  function write(file, after, options) {
    const record = prepareMutation(file, after, options);
    if (!record) return 'unchanged';
    // Recheck after backing up and before replacing the file.
    if (!sameContent(readRecord(record), record.before)) throw new AgentConfigError(`${options.relative}: file changed before replacement`, 'concurrent-modification');
    atomicWrite(file, after, record.mode);
    // Record the mutation BEFORE any metadata/logging call that can throw.
    mutations.set(file, record);
    updateOwnership(record);
    const action = record.before === null ? 'created' : 'updated';
    journal({ action, file: options.relative });
    return action;
  }

  function remove(file, options) {
    const record = prepareMutation(file, null, options);
    if (!record) return 'unchanged';
    if (!sameContent(readRecord(record), record.before)) throw new AgentConfigError(`${options.relative}: file changed before deletion`, 'concurrent-modification');
    fs.rmSync(file);
    mutations.set(file, record);
    updateOwnership(record);
    journal({ action: 'deleted', file: options.relative });
    return 'removed-file';
  }

  function tryRestore(step) {
    const record = mutations.get(step.file);
    if (!record) return true;
    if (!sameContent(readRecord(record), record.after)) {
      journal({ action: 'restore-skipped', file: record.relative, result: 'file-changed-concurrently' });
      return false;
    }
    if (record.before === null) fs.rmSync(record.file, { force: true });
    else atomicWrite(record.file, record.before, record.mode);
    updateOwnership(record, true);
    journal({ action: 'restored', file: record.relative });
    mutations.delete(step.file);
    return true;
  }

  return { readFile, readBuffer, owns, write, remove, tryRestore, hasMutation: (file) => mutations.has(file), backupDir, ownershipPath };
}
