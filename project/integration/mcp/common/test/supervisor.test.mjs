import assert from 'node:assert/strict';
import test from 'node:test';

import { LIMITS } from '../src/limits.mjs';
import { StderrSink, minimalEnvironment, supervise } from '../src/supervisor.mjs';

const ECHO_HOLD = [
  "process.stdin.resume()",
  "process.stderr.write('boot\\n')",
  "process.stdin.on('end', () => process.exit(7))",
].join(';');

test('minimalEnvironment keeps the allowlist and applies extras', () => {
  const before = process.env.TGREP_TEST_MARKER;
  process.env.TGREP_TEST_MARKER = 'inherited';
  try {
    const env = minimalEnvironment({ TGREP_FORCED: '1', PATH: null });
    assert.equal(env.TGREP_TEST_MARKER, undefined);
    assert.equal(env.TGREP_FORCED, '1');
    assert.equal(env.PATH, undefined);
  } finally {
    if (before === undefined) {
      delete process.env.TGREP_TEST_MARKER;
    } else {
      process.env.TGREP_TEST_MARKER = before;
    }
  }
});

test('stop() EOF path lets an obedient child exit by itself', async () => {
  const proc = supervise({
    argv: [process.execPath, '-e', ECHO_HOLD],
    cwd: process.cwd(),
    env: minimalEnvironment(),
    label: 'eof-child',
  });
  assert.ok(proc.alive());
  const exit = await proc.stop();
  assert.equal(exit.code, 7); // the child's own EOF handler ran
});

test('stop() escalates to signals for a stubborn child', async () => {
  // Ignores stdin EOF and (on POSIX) SIGTERM; only SIGKILL stops it there.
  // Windows has no real SIGTERM: kill('SIGTERM') is TerminateProcess, so the
  // escalation terminates the child at that step.
  const stubborn = [
    "process.stdin.resume()",
    "process.on('SIGTERM', () => process.stderr.write('ignored\\n'))",
    "setInterval(() => {}, 1000)",
  ].join(';');
  const proc = supervise({
    argv: [process.execPath, '-e', stubborn],
    cwd: process.cwd(),
    env: minimalEnvironment(),
    label: 'stubborn-child',
  });
  const started = Date.now();
  const exit = await proc.stop();
  assert.equal(exit.code, null);
  if (process.platform === 'win32') {
    assert.equal(exit.signal, 'SIGTERM');
  } else {
    assert.equal(exit.signal, 'SIGKILL');
  }
  // Two grace periods plus force budget; generously bounded for CI variance.
  assert.ok(Date.now() - started < LIMITS.stopGraceMs * 2 + LIMITS.stopForceMs + 5000);
});

test('StderrSink keeps a bounded tail and rotates the log file', async () => {
  const { mkdtempSync, readFileSync, rmSync, statSync } = await import('node:fs');
  const os = await import('node:os');
  const path = await import('node:path');
  const dir = mkdtempSync(path.join(os.tmpdir(), 'mcp-sink-'));
  try {
    const sink = new StderrSink({ filePath: path.join(dir, 'backend.stderr.log'), maxFileBytes: 1024 });
    for (let i = 0; i < 50; i += 1) {
      sink.write(Buffer.from(`line-${i}-${'p'.repeat(40)}\n`, 'utf8'));
    }
    assert.ok(sink.excerpt().includes('line-49'));
    assert.ok(!sink.excerpt().includes('line-0\n'));
    assert.ok(statSync(path.join(dir, 'backend.stderr.log')).size <= 2048);
    assert.ok(readFileSync(path.join(dir, 'backend.stderr.log.old'), 'utf8').length > 0);
    sink.write('decoded stderr from a child: 同步失败\n');
    assert.match(sink.excerpt(), /同步失败/);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('backend exit is observable through exited', async () => {
  const proc = supervise({
    argv: [process.execPath, '-e', 'process.exit(3)'],
    cwd: process.cwd(),
    env: minimalEnvironment(),
  });
  const exit = await proc.exited;
  assert.deepEqual(exit, { code: 3, signal: null });
  assert.equal(proc.alive(), false);
});
