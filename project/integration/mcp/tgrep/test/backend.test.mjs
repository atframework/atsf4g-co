import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { BackendError } from '../../common/src/errors.mjs';
import { WorkspacePaths } from '../../common/src/paths.mjs';
import { TgrepBackend } from '../src/backend.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const FAKE = path.join(HERE, 'fake-tgrep-serve.mjs');

function makePaths() {
  const repo = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-tgrep-repo-'));
  return new WorkspacePaths(repo, path.join(repo, 'build'));
}

async function withBackend(env, run) {
  const paths = makePaths();
  const backend = new TgrepBackend({
    paths,
    binary: FAKE,
    argvOverride: [process.execPath, FAKE],
    stderrLog: path.join(os.tmpdir(), `mcp-tgrep-stderr-${Date.now()}.log`),
  });
  const previous = {};
  for (const [key, value] of Object.entries(env)) {
    previous[key] = process.env[key];
    if (value === undefined) {
      delete process.env[key];
    } else {
      process.env[key] = value;
    }
  }
  try {
    await backend.start();
    await run(backend, paths);
  } finally {
    for (const [key, value] of Object.entries(previous)) {
      if (value === undefined) {
        delete process.env[key];
      } else {
        process.env[key] = value;
      }
    }
    await backend.stop();
    fs.rmSync(paths.repoRoot, { recursive: true, force: true });
  }
}

test('backend starts, answers status, and maps search/files', async () => {
  await withBackend({}, async (backend) => {
    const status = await backend.status();
    assert.equal(status.indexing, false);
    assert.equal(status.hidden_complete, true);

    const search = await backend.search({ pattern: 'hello', detail: true });
    assert.equal(search.num_matches, 2);
    assert.match(search.matches[0].text, /hello/);

    const files = await backend.files({ scope: '' });
    assert.equal(files.files.length, 3);
  });
});

test('backend maps internal error codes to wrapper errors', async () => {
  await withBackend({ TGREP_FAKE_FAIL_METHOD: 'files', TGREP_FAKE_FAIL_CODE: '-32001' }, async (backend) => {
    await backend.status(); // unaffected probe
    await assert.rejects(
      () => backend.files({ scope: '' }),
      (error) => error instanceof BackendError && error.code === 'INDEX_NOT_READY'
    );
  });
});

test('backend start fails cleanly when the binary dies immediately', async () => {
  const paths = makePaths();
  const backend = new TgrepBackend({
    paths,
    binary: 'definitely-not-a-real-binary',
    argvOverride: [process.execPath, '-e', 'process.exit(1)'],
  });
  await assert.rejects(
    () => backend.start(),
    (error) => error instanceof BackendError && error.code === 'BACKEND_FAILED'
  );
  fs.rmSync(paths.repoRoot, { recursive: true, force: true });
});

test('stop() ends the supervised child process', async () => {
  const paths = makePaths();
  const backend = new TgrepBackend({
    paths,
    binary: FAKE,
    argvOverride: [process.execPath, FAKE],
  });
  await backend.start();
  const proc = backend.proc;
  const pid = proc.pid;
  await backend.stop();
  assert.equal(backend.alive(), false);
  // The fake exits on EOF with code 0 (its own handler).
  assert.equal(proc.exitInfo()?.code, 0);
  assert.ok(Number.isInteger(pid));
  fs.rmSync(paths.repoRoot, { recursive: true, force: true });
});
