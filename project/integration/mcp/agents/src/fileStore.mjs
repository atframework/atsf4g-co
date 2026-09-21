/**
 * Repository-contained config writes, build-directory backups, journaled
 * batches, and crash recovery for interrupted ones (see README
 * "崩溃后自动恢复").
 *
 * Every apply opens a lazily created batch in the JSONL journal (batch-begin
 * with the owner pid, one entry per mutation, batch-end with the outcome). A
 * run that crashes mid-batch leaves the batch open; the next mutating run
 * calls recoverInterruptedBatch() first and rolls the affected files back to
 * their pre-batch bytes from the recorded backups. Files that changed after
 * the crash (neither the planned result nor the backup bytes) are skipped and
 * their backups kept for manual inspection.
 */
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { isPidAlive, processStartTimeTicks } from '../../common/src/state.mjs';
import { AgentConfigError } from './errors.mjs';
import { withConfigLock } from './configLock.mjs';

function resolvesInsideRepo(repoRoot, filePath) {
  const root = path.resolve(repoRoot);
  const target = path.resolve(filePath);
  const relative = path.relative(root, target);
  return relative === '' || (!path.isAbsolute(relative) && relative !== '..' && !relative.startsWith(`..${path.sep}`));
}

/** Validate both the lexical path and existing ancestors, including the leaf. */
export function assertConfigPath(repoRoot, filePath) {
  const root = path.resolve(repoRoot);
  const target = path.resolve(filePath);
  const fail = () => { throw new AgentConfigError(`${filePath} resolves outside the repository root`, 'outside-repo'); };
  if (!resolvesInsideRepo(root, target)) fail();
  const realRoot = fs.realpathSync(root);
  let current = target;
  while (true) {
    try {
      const stat = fs.lstatSync(current);
      // A dangling symlink must not masquerade as a missing config.
      let real;
      try { real = fs.realpathSync(current); } catch { fail(); }
      if (!resolvesInsideRepo(realRoot, real)) fail();
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

const digestOf = (bytes) => (bytes === null || bytes === undefined ? null : crypto.createHash('sha256').update(bytes).digest('hex'));
const fileId = (file) => {
  const stat = fs.statSync(file);
  return `${stat.dev}:${stat.ino}:${stat.birthtimeMs}`;
};

/** Staging lives under the configured build directory. EXDEV fails before replacement. */
function atomicReplace(stagingDir, file, bytes, mode = 0o600, beforeRename) {
  fs.mkdirSync(stagingDir, { recursive: true });
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const temp = path.join(stagingDir, `${crypto.randomUUID()}.tmp`);
  let renamed = false;
  try {
    fs.writeFileSync(temp, bytes, { encoding: 'utf8', flag: 'wx', mode });
    if (process.platform !== 'win32') fs.chmodSync(temp, mode);
    if (beforeRename) beforeRename(temp);
    fs.renameSync(temp, file);
    renamed = true;
  } finally {
    if (!renamed) fs.rmSync(temp, { force: true });
  }
}

/** True when the recorded batch owner cannot be the writer anymore (dead pid, or pid reused on Linux). */
function defaultOwnerIsDead({ pid, platform, start_time_ticks }) {
  if (platform && platform !== process.platform) return false;
  if (!Number.isInteger(pid) || pid <= 0) return true;
  if (pid === process.pid) return false; // an open batch in this process must not be recovered under itself
  if (!isPidAlive(pid)) return true;
  if (start_time_ticks !== null && start_time_ticks !== undefined) {
    const current = processStartTimeTicks(pid);
    if (current !== null && String(current) !== String(start_time_ticks)) return true;
  }
  return false;
}

/**
 * Parse the journal and return the batch left open by an interrupted run, or
 * null when every batch is closed (pre-batch-marker entries carry no batch
 * attribution and never count). A torn final line (crash mid-append) is
 * tolerated; any other unreadable line aborts with zero writes.
 */
function analyzeJournal(text, journalPath) {
  const terminated = text.endsWith('\n');
  const lines = text.split('\n');
  if (lines.length > 0 && lines[lines.length - 1] === '') lines.pop();
  const entries = [];
  let tornTail = false;
  let cleanText = null;
  for (let index = 0; index < lines.length; index += 1) {
    const line = lines[index];
    if (line.trim() === '') continue;
    let entry;
    try { entry = JSON.parse(line); } catch {
      if (index === lines.length - 1 && !terminated) {
        tornTail = true;
        // The parseable prefix, re-terminated; appended entries must never
        // follow a torn line, or the next parse meets it in the middle.
        cleanText = lines.slice(0, -1).map((kept) => `${kept}\n`).join('');
        break;
      }
      throw new AgentConfigError(`${journalPath}: unreadable journal line ${index + 1}; fix or remove it before writing configs`, 'journal-corrupted');
    }
    if (!entry || typeof entry !== 'object' || typeof entry.action !== 'string') {
      throw new AgentConfigError(`${journalPath}: invalid journal entry at line ${index + 1}`, 'journal-corrupted');
    }
    entries.push(entry);
  }
  let open = null;
  const invalid = (detail) => { throw new AgentConfigError(`${journalPath}: invalid journal ${detail}`, 'journal-corrupted'); };
  const validBackup = (value) => typeof value === 'string' && /^[\w-]+\.bak$/.test(value);
  for (const entry of entries) {
    if (entry.action === 'batch-begin') {
      if (open) {
        throw new AgentConfigError(`${journalPath}: batch ${open.id} was left open when batch ${entry.batch} started; manual inspection required`, 'journal-corrupted');
      }
      if (typeof entry.batch !== 'string' || !entry.batch || !Number.isInteger(entry.pid) || entry.pid <= 0) invalid('batch owner');
      open = { id: entry.batch, pid: entry.pid, platform: entry.platform, start_time_ticks: entry.start_time_ticks ?? null, files: new Map(), ownershipBackup: undefined };
      continue;
    }
    if (!open) {
      if (entry.batch !== undefined) invalid('entry without an open batch');
      continue;
    }
    if (entry.action === 'batch-end') {
      if (entry.batch !== open.id) {
        throw new AgentConfigError(`${journalPath}: batch-end ${entry.batch} does not close open batch ${open.id}`, 'journal-corrupted');
      }
      open = null;
      continue;
    }
    if (entry.batch !== undefined && entry.batch !== open.id) {
      throw new AgentConfigError(`${journalPath}: entry of batch ${entry.batch} interleaved into open batch ${open.id}`, 'journal-corrupted');
    }
    const fileState = (relative) => {
      if (!open.files.has(relative)) {
        // backup/mode keep the FIRST occurrence in the batch (pre-batch state);
        // Keep all planned results: a later mutation may never reach disk.
        open.files.set(relative, { operation: null, backup: undefined, afterDigests: new Set(), mode: null, beforeFileId: null, restoredFileId: null, restoredInProcess: false, skippedInProcess: null });
      }
      return open.files.get(relative);
    };
    const relative = typeof entry.file === 'string' ? entry.file : null;
    if (['backup', 'planned', 'restored', 'restore-skipped'].includes(entry.action) && !relative) invalid('file');
    if (entry.action === 'backup' && relative) {
      if (!validBackup(entry.backup)) invalid('backup path');
      const state = fileState(relative);
      if (state.backup === undefined) state.backup = entry.backup;
    } else if (entry.action === 'planned' && relative) {
      if (!['create', 'update', 'delete'].includes(entry.operation)) invalid('operation');
      if (entry.operation === 'create' ? entry.backup != null : !validBackup(entry.backup)) invalid('planned backup');
      if (entry.operation === 'delete' ? entry.after_digest !== null : !/^[a-f0-9]{64}$/.test(entry.after_digest ?? '')) invalid('content digest');
      if (entry.operation !== 'create' && (!Number.isInteger(entry.mode) || entry.mode < 0 || entry.mode > 0o777)) invalid('file mode');
      const state = fileState(relative);
      if (!state.operation) {
        state.backup = entry.backup ?? null;
        state.mode = entry.mode ?? null;
        state.beforeFileId = entry.before_file_id ?? null;
      }
      state.operation = entry.operation;
      state.afterDigests.add(entry.after_digest);
      state.restoredInProcess = false;
      state.skippedInProcess = null;
    } else if (entry.action === 'restored' && relative) {
      fileState(relative).restoredInProcess = true;
    } else if (entry.action === 'restore-skipped' && relative) {
      fileState(relative).skippedInProcess = entry.result ?? 'restore-skipped';
    } else if (['restore-planned', 'recovery-planned'].includes(entry.action)) {
      if (!relative || typeof entry.restored_file_id !== 'string') invalid('restore identity');
      fileState(relative).restoredFileId = entry.restored_file_id;
    } else if (entry.action === 'ownership-backup') {
      if (entry.backup !== null && !validBackup(entry.backup)) invalid('ownership backup path');
      open.ownershipBackup = entry.backup ?? null;
    } else if (!['created', 'updated', 'deleted', 'recovered', 'recovery-skipped'].includes(entry.action)) {
      invalid(`action ${entry.action}`);
    }
  }
  return { open, tornTail, cleanText };
}

/** Read-only peek: is there an interrupted batch waiting for recovery? */
export function findOpenJournalBatch({ tmpDir }) {
  const journalPath = path.join(tmpDir, 'agent-config-journal.jsonl');
  let text;
  try { text = fs.readFileSync(journalPath, 'utf8'); } catch (error) {
    if (error.code === 'ENOENT') return null;
    throw error;
  }
  const { open, tornTail } = analyzeJournal(text, journalPath);
  return open ? { batchId: open.id, pid: open.pid, tornTail } : null;
}

/**
 * Roll back the batch left open by a crashed run before this one continues.
 * Returns null when there is nothing to recover; otherwise a
 * report of restored/removed/skipped files. Throws (zero writes) when the
 * journal is damaged or the interrupted owner still looks alive.
 */
export function recoverInterruptedBatch(options) {
  const { tmpDir, ownerIsDead = defaultOwnerIsDead } = options;
  const journalPath = path.join(tmpDir, 'agent-config-journal.jsonl');
  let text;
  try { text = fs.readFileSync(journalPath, 'utf8'); } catch (error) {
    if (error.code === 'ENOENT') return null;
    throw error;
  }
  const { open, tornTail } = analyzeJournal(text, journalPath);
  if (open && !ownerIsDead(open)) throw new AgentConfigError(`agent-config batch owner (pid ${open.pid}, platform ${open.platform ?? 'unknown'}) is still running or cannot be checked on this platform`, 'journal-batch-active');
  if (!open && !tornTail && (!text || text.endsWith('\n'))) return null;
  // Re-read under the same exclusion used by writers; another recovery may
  // have finished since the read-only check above.
  return withConfigLock(tmpDir, () => recoverUnlocked(options));
}

function recoverUnlocked({ repoRoot, stateDir, tmpDir, ownerIsDead = defaultOwnerIsDead }) {
  const backupDir = path.join(tmpDir, 'agent-config-backups');
  const stagingDir = path.join(tmpDir, 'agent-config-staging');
  const journalPath = path.join(tmpDir, 'agent-config-journal.jsonl');
  let text;
  try { text = fs.readFileSync(journalPath, 'utf8'); } catch (error) {
    if (error.code === 'ENOENT') return null;
    throw error;
  }
  const { open: batch, tornTail, cleanText } = analyzeJournal(text, journalPath);
  if (batch && !ownerIsDead(batch)) {
    throw new AgentConfigError(
      `agent-config journal has an unfinished batch ${batch.id} whose owner (pid ${batch.pid}) is still running; retry after it exits`,
      'journal-batch-active',
    );
  }
  // Normalize a torn/unfinished final line before any append: entries written
  // after it would leave an unparseable line mid-file and abort every future
  // run. The torn bytes are a crash artifact and carry no committed entry.
  if (text.length > 0 && (tornTail || !text.endsWith('\n'))) {
    let journalMode = 0o600;
    try { journalMode = fs.statSync(journalPath).mode & 0o777; } catch { /* keep default */ }
    atomicReplace(stagingDir, journalPath, tornTail ? cleanText : `${text}\n`, journalMode);
  }
  if (!batch) return null;
  const append = (entry) => fs.appendFileSync(journalPath, `${JSON.stringify({ time: new Date().toISOString(), batch: batch.id, ...entry })}\n`, { encoding: 'utf8', mode: 0o600 });
  const report = { batchId: batch.id, backupDir, restored: [], removed: [], skipped: [], ownershipRestored: null, tornTail };
  const restoredIds = new Map();
  const skip = (relative, reason) => {
    report.skipped.push({ file: relative, reason });
    append({ action: 'recovery-skipped', file: relative, reason });
  };
  for (const [relative, state] of batch.files) {
    if (state.skippedInProcess || !state.operation) continue;
    const file = path.resolve(repoRoot, relative);
    // The same containment proof as the write path: a journaled path whose
    // ancestors were replaced by an escaping link after the crash must not
    // be written through, and a symlinked leaf is never removed or replaced.
    try {
      assertConfigPath(repoRoot, file);
    } catch (error) {
      skip(relative, error instanceof AgentConfigError ? error.kind : 'outside-repo');
      continue;
    }
    let current = null;
    let stat = null;
    try {
      stat = fs.lstatSync(file);
      current = fs.readFileSync(file);
    } catch (error) {
      if (error.code !== 'ENOENT') { skip(relative, 'unreadable'); continue; }
    }
    // No backup means the batch's first mutation of this file created it:
    // the pre-batch state is "absent".
    if (state.backup === null) {
      if (current === null) {
        append({ action: 'recovered', file: relative, result: 'never-written' });
      } else if (state.afterDigests.has(digestOf(current))) {
        fs.rmSync(file, { force: true });
        report.removed.push(relative);
        append({ action: 'recovered', file: relative, result: 'removed-creation' });
      } else {
        skip(relative, 'file-changed-after-crash');
      }
      continue;
    }
    let before = null;
    try {
      assertConfigPath(tmpDir, path.join(backupDir, state.backup));
      before = fs.readFileSync(path.join(backupDir, state.backup));
    } catch {
      skip(relative, 'missing-backup');
      continue;
    }
    if (current !== null && before.equals(current)) {
      const currentId = fileId(file);
      if (currentId === state.beforeFileId || currentId === state.restoredFileId) restoredIds.set(relative, currentId);
      // Already at pre-batch bytes: the batch's write never reached the disk.
      append({ action: 'recovered', file: relative, result: 'already-before-state' });
      continue;
    }
    const ours = state.afterDigests.has(digestOf(current));
    if (!ours) { skip(relative, 'file-changed-after-crash'); continue; }
    const mode = state.mode ?? (stat !== null ? stat.mode & 0o777 : 0o600);
    atomicReplace(stagingDir, file, before, mode, (temp) => {
      append({ action: 'recovery-planned', file: relative, restored_file_id: fileId(temp) });
    });
    restoredIds.set(relative, fileId(file));
    report.restored.push(relative);
    append({ action: 'recovered', file: relative, result: state.operation === 'delete' ? 'restored-deleted' : 'restored-update' });
  }
  if (batch.ownershipBackup !== undefined) {
    const ownershipPath = path.join(stateDir, 'agent-config-state.json');
    try {
      if (batch.ownershipBackup === null) {
        fs.rmSync(ownershipPath, { force: true });
      } else {
        assertConfigPath(tmpDir, path.join(backupDir, batch.ownershipBackup));
        const ownership = JSON.parse(fs.readFileSync(path.join(backupDir, batch.ownershipBackup), 'utf8'));
        if (!ownership?.files || typeof ownership.files !== 'object' || Array.isArray(ownership.files)) throw new Error('invalid ownership backup');
        for (const [relative, state] of batch.files) {
          const entry = ownership.files[relative];
          if (!state.operation) continue;
          if (restoredIds.has(relative) && state.beforeFileId && entry?.file_id === state.beforeFileId) {
            entry.file_id = restoredIds.get(relative);
          } else delete ownership.files[relative];
        }
        atomicReplace(stagingDir, ownershipPath, `${JSON.stringify(ownership, null, 2)}\n`);
      }
      report.ownershipRestored = true;
      append({ action: 'recovered', file: '(ownership)', result: 'restored' });
    } catch {
      report.ownershipRestored = false;
      // A missing snapshot must not leave this batch's newly acquired claims
      // in place. Preserve unrelated records, but drop every affected claim.
      // If even this cannot be saved, keep the batch open and stop the run.
      let ownership = { files: {} };
      try { ownership = JSON.parse(fs.readFileSync(ownershipPath, 'utf8')); } catch (error) {
        if (error.code !== 'ENOENT') throw error;
      }
      if (!ownership?.files || typeof ownership.files !== 'object' || Array.isArray(ownership.files)) throw new AgentConfigError('cannot safely restore ownership metadata', 'invalid-state');
      for (const [relative, state] of batch.files) if (state.operation) delete ownership.files[relative];
      atomicReplace(stagingDir, ownershipPath, `${JSON.stringify(ownership, null, 2)}\n`);
      append({ action: 'recovery-skipped', file: '(ownership)', reason: 'restore-failed' });
    }
  }
  append({ action: 'batch-end', pid: process.pid, status: 'crash-recovered', restored: report.restored.length, removed: report.removed.length, skipped: report.skipped.length });
  return report;
}

export function createFileStore({ repoRoot, stateDir, tmpDir }) {
  const backupDir = path.join(tmpDir, 'agent-config-backups');
  const stagingDir = path.join(tmpDir, 'agent-config-staging');
  const journalPath = path.join(tmpDir, 'agent-config-journal.jsonl');
  const ownershipPath = path.join(stateDir, 'agent-config-state.json');
  const mutations = new Map();
  let batchId = null;
  let ownershipSnapshotted = false;
  const readFile = (file) => readConfigFile(repoRoot, file);
  const readBuffer = (file) => readConfigFile(repoRoot, file, null);
  const sameContent = (left, right) => Buffer.isBuffer(left) && Buffer.isBuffer(right) ? left.equals(right) : left === right;
  const readRecord = (record) => record.binary ? readBuffer(record.file) : readFile(record.file);

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

  function journal(entry) {
    fs.mkdirSync(backupDir, { recursive: true });
    fs.appendFileSync(journalPath, `${JSON.stringify({ time: new Date().toISOString(), ...entry })}\n`, { encoding: 'utf8', mode: 0o600 });
  }

  const journalBatched = (entry) => journal(batchId === null ? entry : { batch: batchId, ...entry });

  /** Open the journal batch before the batch's first real mutation (unchanged steps never open one). */
  function openBatchIfNeeded() {
    if (batchId !== null) return;
    const open = findOpenJournalBatch({ tmpDir });
    if (open) throw new AgentConfigError(`agent-config batch ${open.batchId} is unfinished; its owner may be still running; recover it before writing`, 'journal-batch-active');
    const id = crypto.randomUUID();
    journal({ action: 'batch-begin', batch: id, pid: process.pid, platform: process.platform, start_time_ticks: processStartTimeTicks() });
    batchId = id;
    ownershipSnapshotted = false;
  }

  function closeBatch(status) {
    if (batchId === null) return false;
    const id = batchId;
    journal({ action: 'batch-end', batch: id, pid: process.pid, status });
    batchId = null;
    return true;
  }

  function saveOwnership(data) {
    atomicReplace(stagingDir, ownershipPath, `${JSON.stringify(data, null, 2)}\n`);
  }

  function owns(relative) {
    const entry = loadOwnership().files[relative];
    const file = path.join(repoRoot, relative);
    if (entry?.created_by !== 'setup' || !entry.file_id) return false;
    if (readFile(file) === null) return false;
    return entry.file_id === fileId(file);
  }

  function prepareMutation(file, after, { relative, expectedBefore }) {
    const binary = Buffer.isBuffer(after) || Buffer.isBuffer(expectedBefore);
    const before = binary ? readBuffer(file) : readFile(file);
    if (!sameContent(before, expectedBefore)) {
      throw new AgentConfigError(`${relative}: file changed concurrently since the plan was computed; aborting`, 'concurrent-modification');
    }
    if (sameContent(before, after)) return null;
    openBatchIfNeeded();
    const ownership = loadOwnership().files[relative];
    snapshotOwnership();
    const mode = before === null ? 0o600 : fs.statSync(file).mode & 0o777;
    let backupName;
    if (before !== null) {
      fs.mkdirSync(backupDir, { recursive: true });
      backupName = `${crypto.randomUUID()}.bak`;
      fs.writeFileSync(path.join(backupDir, backupName), before, { encoding: 'utf8', flag: 'wx', mode: 0o600 });
      journalBatched({ action: 'backup', file: relative, backup: backupName });
    }
    journalBatched({
      action: 'planned', file: relative, operation: after === null ? 'delete' : before === null ? 'create' : 'update',
      backup: backupName, after_digest: digestOf(after), mode: before === null ? null : mode,
      before_file_id: before === null ? null : fileId(file),
    });
    return { file, relative, before, after, binary, ownership, mode, wasOwned: before !== null && ownership?.created_by === 'setup' && ownership.file_id === fileId(file) };
  }

  function snapshotOwnership() {
    if (batchId !== null && !ownershipSnapshotted) {
      // Snapshot the pre-batch ownership bytes once so crash recovery can undo this batch's records.
      let bytes = null;
      try { bytes = fs.readFileSync(ownershipPath); } catch (error) {
        if (error.code !== 'ENOENT') throw error;
      }
      if (bytes !== null) {
        fs.mkdirSync(backupDir, { recursive: true });
        const name = `ownership-${crypto.randomUUID()}.bak`;
        fs.writeFileSync(path.join(backupDir, name), bytes, { flag: 'wx', mode: 0o600 });
        journalBatched({ action: 'ownership-backup', backup: name });
      } else {
        journalBatched({ action: 'ownership-backup', backup: null });
      }
      ownershipSnapshotted = true;
    }
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

  function writeUnlocked(file, after, options) {
    const record = prepareMutation(file, after, options);
    if (!record) return 'unchanged';
    // Recheck after backing up and before replacing the file.
    if (!sameContent(readRecord(record), record.before)) throw new AgentConfigError(`${options.relative}: file changed before replacement`, 'concurrent-modification');
    atomicReplace(stagingDir, file, after, record.mode);
    // Record the mutation BEFORE any metadata/logging call that can throw.
    mutations.set(file, record);
    updateOwnership(record);
    const action = record.before === null ? 'created' : 'updated';
    journalBatched({ action, file: options.relative });
    return action;
  }

  function removeUnlocked(file, options) {
    const record = prepareMutation(file, null, options);
    if (!record) return 'unchanged';
    if (!sameContent(readRecord(record), record.before)) throw new AgentConfigError(`${options.relative}: file changed before deletion`, 'concurrent-modification');
    fs.rmSync(file);
    mutations.set(file, record);
    updateOwnership(record);
    journalBatched({ action: 'deleted', file: options.relative });
    return 'removed-file';
  }

  function tryRestore(step) {
    const record = mutations.get(step.file);
    if (!record) return true;
    if (!sameContent(readRecord(record), record.after)) {
      journalBatched({ action: 'restore-skipped', file: record.relative, result: 'file-changed-concurrently' });
      return false;
    }
    if (record.before === null) fs.rmSync(record.file, { force: true });
    else atomicReplace(stagingDir, record.file, record.before, record.mode, (temp) => {
      journalBatched({ action: 'restore-planned', file: record.relative, restored_file_id: fileId(temp) });
    });
    updateOwnership(record, true);
    journalBatched({ action: 'restored', file: record.relative });
    mutations.delete(step.file);
    return true;
  }

  return {
    readFile, readBuffer, owns, tryRestore,
    write: (...args) => withConfigLock(tmpDir, () => writeUnlocked(...args)),
    remove: (...args) => withConfigLock(tmpDir, () => removeUnlocked(...args)),
    hasMutation: (file) => mutations.has(file),
    /** Close the journal batch; false when no mutation ever opened one. */
    finishBatch: closeBatch,
    backupDir, ownershipPath,
  };
}
