/**
 * Service states, atomic state records, and per-tool instance locks.
 *
 * State machine:
 *   STARTING -> INITIALIZING/OPENING -> READY or DEGRADED
 *   any stage -> STOPPING -> STOPPED
 *   startup failure -> STOPPING -> FAILED
 *
 * The instance lock is an O_EXCL lock file plus a holder record. Dead holders
 * are detected by pid liveness (with the Linux process start time when
 * readable) and stolen once; a lock file that cannot be removed is treated as
 * held -- fail-safe towards INDEX_IN_USE, never towards deleting a live lock.
 * The backends' own exclusivity (tgrep serve.lock, CodeGraph writer.pid) is
 * the final consistency guarantee.
 */

import { randomUUID } from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

import { IndexInUse } from './errors.mjs';

export const ServiceState = Object.freeze({
  STARTING: 'starting',
  INITIALIZING: 'initializing',
  OPENING: 'opening',
  READY: 'ready',
  DEGRADED: 'degraded',
  STOPPING: 'stopping',
  STOPPED: 'stopped',
  FAILED: 'failed',
});

/** Linux-only: raw starttime field of /proc/<pid>/stat (clock ticks); used for
 *  pid-reuse-resistant identity comparisons. null where unavailable. */
export function processStartTimeTicks(pid = process.pid) {
  if (process.platform !== 'linux') {
    return null;
  }
  try {
    const stat = fs.readFileSync(`/proc/${pid}/stat`, 'utf8');
    // The comm field can contain spaces and parens; everything after the last ')' is fixed-width fields.
    const fields = stat.slice(stat.lastIndexOf(')') + 2).split(' ');
    return fields[19] ?? null; // field 22 overall (state is 3): index 19 after zero-based offset
  } catch {
    return null;
  }
}

export function currentIdentity() {
  return {
    pid: process.pid,
    instance_id: randomUUID(),
    start_time_ticks: processStartTimeTicks(),
  };
}

export function isPidAlive(pid) {
  if (!Number.isInteger(pid) || pid <= 0) {
    return false;
  }
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    return error.code === 'EPERM'; // exists but not ours to signal
  }
}

function holderIsStale(holder) {
  const pid = holder?.identity?.pid;
  if (!Number.isInteger(pid)) {
    return true; // unreadable record: assume stale once, steal attempt is guarded by removal
  }
  if (!isPidAlive(pid)) {
    return true;
  }
  const recorded = holder.identity.start_time_ticks;
  if (recorded !== null && recorded !== undefined) {
    const current = processStartTimeTicks(pid);
    if (current !== null && current !== String(recorded) && current !== recorded) {
      return true; // pid was reused by a different process
    }
  }
  return false;
}

export class StateStore {
  constructor(filePath) {
    this.path = filePath;
  }

  write(payload) {
    fs.mkdirSync(path.dirname(this.path), { recursive: true });
    const tempPath = `${this.path}.${randomUUID()}.tmp`;
    try {
      fs.writeFileSync(tempPath, `${JSON.stringify(payload, null, 2)}\n`, 'utf8');
      fs.renameSync(tempPath, this.path);
    } finally {
      try {
        fs.unlinkSync(tempPath);
      } catch {
        /* renamed or never created */
      }
    }
  }

  read() {
    try {
      return JSON.parse(fs.readFileSync(this.path, 'utf8'));
    } catch {
      return null;
    }
  }
}

export class ToolInstanceLock {
  constructor(directory, tool) {
    this.path = path.join(directory, `${tool}.instance.lock`);
    this.holderPath = path.join(directory, `${tool}.holder.json`);
    this.handle = null;
    this.locked = false;
  }

  acquire(identity, repoRoot, stage) {
    fs.mkdirSync(path.dirname(this.path), { recursive: true });
    let handle = null;
    try {
      handle = fs.openSync(this.path, 'wx');
    } catch (error) {
      if (error.code !== 'EEXIST') {
        throw error;
      }
      const holder = new StateStore(this.holderPath).read();
      if (!holderIsStale(holder)) {
        throw new IndexInUse('another wrapper instance holds this tool\'s lock', {
          lock_file: path.basename(this.path),
          holder,
        });
      }
      // Steal once: remove the dead holder's files and retry. Removal failure
      // (e.g. Windows: file still held open) means the holder is alive after
      // all -- fail safe.
      try {
        fs.unlinkSync(this.path);
      } catch {
        /* fall through to the retry, which will fail with EEXIST if held */
      }
      try {
        fs.unlinkSync(this.holderPath);
      } catch {
        /* diagnostics only */
      }
      try {
        handle = fs.openSync(this.path, 'wx');
      } catch (retryError) {
        if (retryError.code === 'EEXIST') {
          throw new IndexInUse('another wrapper instance holds this tool\'s lock', {
            lock_file: path.basename(this.path),
            holder: new StateStore(this.holderPath).read(),
          });
        }
        throw retryError;
      }
    }
    this.handle = handle;
    this.locked = true;
    fs.writeFileSync(handle, `${JSON.stringify({ identity, repo_root: repoRoot, stage })}\n`, 'utf8');
    new StateStore(this.holderPath).write({ identity, repo_root: repoRoot, stage });
  }

  updateStage(stage) {
    if (!this.locked) {
      return;
    }
    const store = new StateStore(this.holderPath);
    const record = store.read() ?? {};
    record.stage = stage;
    try {
      store.write(record);
    } catch {
      /* diagnostics only */
    }
  }

  release() {
    if (!this.locked) {
      return;
    }
    this.locked = false;
    try {
      this.handle?.close();
    } catch {
      /* already closed */
    }
    this.handle = null;
    try {
      fs.unlinkSync(this.path);
    } catch {
      /* already gone */
    }
    try {
      fs.unlinkSync(this.holderPath);
    } catch {
      /* already gone */
    }
  }

  readHolder() {
    return new StateStore(this.holderPath).read();
  }
}
