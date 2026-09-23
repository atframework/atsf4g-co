/**
 * Supervised child processes for the line-protocol backends (tgrep).
 *
 * Lifecycle contract (see README 行为要点): stop() sends stdin EOF first (the
 * documented graceful-stop signal for both backends), waits the grace period,
 * then SIGTERM, waits again, then SIGKILL -- only against the pid this
 * instance created. If the shared owner is killed, the backend's stdin pipe
 * breaks and the backend exits on its own (upstream-verified behavior); no
 * Job Object or guardian process is used.
 *
 * Subprocess creation always uses an argument array, a fixed cwd, shell
 * disabled, and a hidden window. The environment is a minimal allowlist plus
 * tool-specific forced variables; inherited host variables are dropped.
 */

import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

import { LIMITS } from './limits.mjs';

const WINDOWS_ENV_ALLOWLIST = [
  'SYSTEMROOT', 'SYSTEMDRIVE', 'WINDIR', 'TEMP', 'TMP', 'PATHEXT', 'COMSPEC',
  'PATH', 'NUMBER_OF_PROCESSORS', 'PROCESSOR_ARCHITECTURE',
];
const POSIX_ENV_ALLOWLIST = ['PATH', 'HOME', 'LANG', 'LC_CTYPE', 'TMPDIR'];

export function minimalEnvironment(extra = null) {
  const allowlist = process.platform === 'win32' ? WINDOWS_ENV_ALLOWLIST : POSIX_ENV_ALLOWLIST;
  const env = {};
  for (const name of allowlist) {
    if (process.env[name] !== undefined) {
      env[name] = process.env[name];
    }
  }
  if (process.platform !== 'win32' && env.TERM === undefined) {
    env.TERM = 'dumb';
  }
  if (extra) {
    for (const [name, value] of Object.entries(extra)) {
      if (value === null || value === undefined) {
        delete env[name];
      } else {
        env[name] = String(value);
      }
    }
  }
  return env;
}

/**
 * Bounded stderr sink: keeps an in-memory tail and appends to a log file,
 * rotating it when it exceeds `maxFileBytes` (current -> .old).
 */
export class StderrSink {
  constructor({ filePath = null, maxTailBytes = 64 * 1024, maxFileBytes = 256 * 1024 } = {}) {
    this.filePath = filePath;
    this.maxTailBytes = maxTailBytes;
    this.maxFileBytes = maxFileBytes;
    this.chunks = [];
    this.total = 0;
    this.fileTotal = 0;
    if (filePath) {
      fs.mkdirSync(path.dirname(filePath), { recursive: true });
      this.fileTotal = fs.statSync(filePath, { throwIfNoEntry: false })?.size ?? 0;
    }
  }

  write(chunk) {
    if (typeof chunk === 'string') chunk = Buffer.from(chunk, 'utf8');
    this.chunks.push(chunk);
    this.total += chunk.length;
    while (this.total > this.maxTailBytes && this.chunks.length > 1) {
      const dropped = this.chunks.shift();
      this.total -= dropped.length;
    }
    if (!this.filePath) {
      return;
    }
    try {
      if (this.fileTotal >= this.maxFileBytes) {
        const old = `${this.filePath}.old`;
        try {
          fs.unlinkSync(old);
        } catch {
          /* no previous rotation */
        }
        fs.renameSync(this.filePath, old);
        this.fileTotal = 0;
      }
      fs.appendFileSync(this.filePath, chunk);
      this.fileTotal += chunk.length;
    } catch {
      /* log writes are best-effort */
    }
  }

  excerpt(maxChars = 2000) {
    const text = Buffer.concat(this.chunks).toString('utf8');
    return text.slice(Math.max(0, text.length - maxChars));
  }
}

function delay(ms) {
  return new Promise((resolve) => {
    setTimeout(resolve, ms);
  });
}

/**
 * Spawn one supervised backend process.
 *
 * @param {object} options
 * @param {string[]} options.argv command + args; never a shell string
 * @param {string} options.cwd
 * @param {Record<string,string>} options.env
 * @param {string} [options.label]
 * @param {StderrSink} [options.stderrSink]
 */
export function supervise({ argv, cwd, env, label = 'backend', stderrSink = null }) {
  const sink = stderrSink ?? new StderrSink();
  const child = spawn(argv[0], argv.slice(1), {
    cwd,
    env,
    stdio: ['pipe', 'pipe', 'pipe'],
    shell: false,
    windowsHide: true,
  });

  child.stderr?.setEncoding('utf8');
  child.stderr?.on('data', (chunk) => sink.write(chunk));

  let exitInfo = null;
  const exited = new Promise((resolve) => {
    child.once('exit', (code, signal) => {
      exitInfo = { code, signal };
      resolve(exitInfo);
    });
  });

  const alive = () => exitInfo === null;
  const waitExit = async (timeoutMs) => {
    if (exitInfo !== null) {
      return exitInfo;
    }
    let timer = null;
    try {
      return await Promise.race([
        exited,
        new Promise((resolve) => {
          timer = setTimeout(() => resolve(null), timeoutMs);
        }),
      ]);
    } finally {
      clearTimeout(timer);
    }
  };
  const closeStdin = () => {
    try {
      child.stdin?.end();
    } catch {
      /* already closed */
    }
  };

  let stopPromise = null;
  const stop = async () => {
    if (stopPromise) {
      return stopPromise;
    }
    stopPromise = (async () => {
      closeStdin();
      if (await waitExit(LIMITS.stopGraceMs)) {
        return exitInfo;
      }
      try {
        child.kill('SIGTERM');
      } catch {
        /* already gone */
      }
      if (await waitExit(LIMITS.stopGraceMs)) {
        return exitInfo;
      }
      try {
        child.kill('SIGKILL');
      } catch {
        /* already gone */
      }
      return waitExit(LIMITS.stopForceMs).then(() => exitInfo);
    })();
    return stopPromise;
  };

  return {
    child,
    pid: child.pid ?? null,
    label,
    stderrSink: sink,
    alive,
    exitInfo: () => exitInfo,
    exited,
    waitExit,
    closeStdin,
    stop,
  };
}
