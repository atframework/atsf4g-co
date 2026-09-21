/** Short, nonblocking exclusion for config mutations and crash recovery. */
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { isPidAlive, processStartTimeTicks } from '../../common/src/state.mjs';
import { AgentConfigError } from './errors.mjs';

export function withConfigLock(tmpDir, action) {
  const directory = path.join(tmpDir, 'agent-config-locks');
  fs.mkdirSync(directory, { recursive: true });
  const name = `${crypto.randomUUID()}.lock`;
  const claim = path.join(directory, name);
  const pending = `${claim}.tmp`;
  const owner = { pid: process.pid, platform: process.platform, start_time_ticks: processStartTimeTicks() };
  try {
    // Publish a complete record atomically. Unique names mean a stale claim
    // can never be confused with a new owner's lock when it is removed.
    fs.writeFileSync(pending, JSON.stringify(owner), { flag: 'wx', mode: 0o600 });
    fs.renameSync(pending, claim);
    // Every contender publishes before scanning. If two overlap, at least
    // one observes the other and aborts before touching shared state.
    for (const other of fs.readdirSync(directory)) {
      if (other === name || !other.endsWith('.lock')) continue;
      const otherPath = path.join(directory, other);
      let record;
      try { record = JSON.parse(fs.readFileSync(otherPath, 'utf8')); } catch (error) {
        if (error.code === 'ENOENT') continue; // its owner released it
      }
      let dead = false;
      if (record?.platform === process.platform && Number.isInteger(record.pid) && record.pid > 0) {
        dead = !isPidAlive(record.pid);
        const ticks = record.start_time_ticks == null ? null : processStartTimeTicks(record.pid);
        if (ticks !== null && ticks !== String(record.start_time_ticks)) dead = true;
      }
      if (!dead) throw new AgentConfigError('another config writer or recovery is still running; retry after it exits', 'journal-batch-active');
      fs.rmSync(otherPath, { force: true });
    }
    return action();
  } finally {
    fs.rmSync(pending, { force: true });
    fs.rmSync(claim, { force: true });
  }
}
