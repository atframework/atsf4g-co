import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { WorkspacePaths } from '../../../common/src/paths.mjs';
import { CodeGraphBackend, indexDirName, resolveIndexSelection, runInitializer } from '../src/backend.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const FAKE = path.join(HERE, 'fake-codegraph.mjs');

test('watcher diagnostics preserve partial lines and expose degraded refresh', () => {
  const backend = new CodeGraphBackend({ paths: { repoRoot: HERE }, runtime: 'unused', cliEntry: 'unused', dirName: null });
  backend.observeDiagnostics('[CodeGraph MCP] File watcher act');
  assert.equal(backend.syncStatus().watcher, 'starting');
  backend.observeDiagnostics('ive — graph will auto-sync on changes\n');
  assert.equal(backend.syncStatus().watcher, 'active');
  backend.observeDiagnostics('[CodeGraph MCP] Auto-synced 2 file(s) in 20ms\n');
  assert.ok(backend.syncStatus().last_sync_unix);
  backend.observeDiagnostics('[CodeGraph MCP] File watcher degraded — test failure\n');
  assert.equal(backend.syncStatus().watcher, 'degraded');
  assert.match(backend.syncStatus().last_error, /test failure/);
  assert.equal(backend.syncStatus().mode, 'session');
});

test('cancelling the first index stops its child before returning', async () => {
  const paths = makePaths();
  const controller = new AbortController();
  const pidFile = path.join(paths.repoRoot, 'initializer.pid');
  fs.writeFileSync(path.join(paths.repoRoot, 'package.json'), '{"type":"module"}');
  fs.writeFileSync(path.join(paths.repoRoot, 'index.js'), `
    import fs from 'node:fs';
    export class CodeGraph {
      static isInitialized() { return false; }
      static async init() {
        const file = ${JSON.stringify(pidFile)};
        fs.writeFileSync(file + '.tmp', String(process.pid));
        fs.renameSync(file + '.tmp', file);
        return new Promise(() => setInterval(() => {}, 1000));
      }
    }
  `);
  const task = runInitializer({ runtime: process.execPath, repoRoot: paths.repoRoot, libraryDir: paths.repoRoot, dirName: null, signal: controller.signal });
  // Attach the rejection handler before abort, including when startup itself fails.
  const settled = task.then(value => ({ value }), error => ({ error }));
  try {
    const deadline = Date.now() + 10000;
    while (!fs.existsSync(pidFile) && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 25));
    assert.ok(fs.existsSync(pidFile), 'initializer must start the controlled library');
    const pid = Number(fs.readFileSync(pidFile, 'utf8'));
    assert.ok(Number.isInteger(pid) && pid > 0, 'ready record must contain the actual child PID');
    controller.abort();
    assert.equal((await settled).error?.name, 'AbortError');
    assert.throws(() => process.kill(pid, 0), 'the initializer must exit before cancellation resolves');
  } finally {
    controller.abort();
    await settled;
    fs.rmSync(paths.repoRoot, { recursive: true, force: true });
  }
});

function makePaths() {
  const repo = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-cg-repo-'));
  return new WorkspacePaths(repo, path.join(repo, 'build'));
}

async function withBackend(run, extraEnv = {}) {
  const paths = makePaths();
  const backend = new CodeGraphBackend({
    paths,
    runtime: FAKE,
    cliEntry: FAKE,
    argvOverride: [process.execPath, FAKE],
    dirName: null,
  });
  const previous = {};
  for (const [key, value] of Object.entries(extraEnv)) {
    previous[key] = process.env[key];
    process.env[key] = value;
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

test('backend handshakes, lists tools, and calls tools', async () => {
  await withBackend(async (backend) => {
    const listed = await backend.listTools();
    assert.ok(listed.tools.length >= 2);
    const names = listed.tools.map((tool) => tool.name);
    assert.ok(names.includes('codegraph_explore'));
    assert.ok(names.includes('codegraph_status'));

    const result = await backend.callTool('codegraph_explore', { query: 'team manager' });
    assert.equal(result.isError, undefined);
    assert.equal(result.content[0].text, 'fake explore result');
    assert.equal(result.structuredContent.echoed_query, 'team manager');
  });
});

test('backend stop() ends the fake child', async () => {
  await withBackend(async (backend) => {
    assert.ok(backend.transport.pid);
    const pid = backend.transport.pid;
    await backend.stop();
    const deadline = Date.now() + 5000;
    let exited = false;
    while (Date.now() < deadline) {
      try {
        process.kill(pid, 0);
        await new Promise((resolve) => {
          setTimeout(resolve, 100);
        });
      } catch {
        exited = true;
        break;
      }
    }
    assert.ok(exited, 'fake backend must exit after client close');
  });
});

test('buildSpawnSpec forces the direct-mode environment and clears injections', async () => {
  const paths = makePaths();
  const backend = new CodeGraphBackend({ paths, runtime: 'r', cliEntry: 'c', dirName: '.codegraph-workspace-test' });
  const previous = {
    NODE_OPTIONS: process.env.NODE_OPTIONS,
    CODEGRAPH_DAEMON_INTERNAL: process.env.CODEGRAPH_DAEMON_INTERNAL,
  };
  process.env.NODE_OPTIONS = '--require /evil.js';
  process.env.CODEGRAPH_DAEMON_INTERNAL = '1';
  try {
    const { argv, env } = backend.buildSpawnSpec();
    assert.equal(argv[0], 'r');
    assert.deepEqual(argv.slice(-4), ['serve', '--mcp', '--path', paths.repoRoot]);
    assert.equal(env.CODEGRAPH_NO_DAEMON, '1');
    assert.equal(env.DO_NOT_TRACK, '1');
    assert.equal(env.CODEGRAPH_TELEMETRY, '0');
    assert.equal(env.CODEGRAPH_NO_UPDATE_CHECK, '1');
    assert.equal(env.CODEGRAPH_NO_DOWNLOAD, '1');
    assert.equal(env.CODEGRAPH_STARTUP_HANDSHAKE_TIMEOUT_MS, '0');
    assert.equal(env.CODEGRAPH_DIR, '.codegraph-workspace-test');
    assert.ok(env.CODEGRAPH_MCP_TOOLS.startsWith('codegraph_explore,codegraph_status'));
    assert.equal(env.NODE_OPTIONS, undefined);
    assert.equal(env.CODEGRAPH_DAEMON_INTERNAL, undefined);
  } finally {
    for (const [key, value] of Object.entries(previous)) {
      if (value === undefined) {
        delete process.env[key];
      } else {
        process.env[key] = value;
      }
    }
    fs.rmSync(paths.repoRoot, { recursive: true, force: true });
  }
});

test('resolveIndexSelection prefers an existing index over creating one', async () => {
  const paths = makePaths();
  try {
    assert.deepEqual(resolveIndexSelection(paths.repoRoot), {
      dirName: indexDirName(paths.repoRoot),
      needsInit: true,
    });
    fs.mkdirSync(path.join(paths.repoRoot, '.codegraph'), { recursive: true });
    fs.writeFileSync(path.join(paths.repoRoot, '.codegraph', 'codegraph.db'), '');
    assert.deepEqual(resolveIndexSelection(paths.repoRoot), { dirName: null, needsInit: false });
  } finally {
    fs.rmSync(paths.repoRoot, { recursive: true, force: true });
  }
});
