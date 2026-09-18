import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

import { WorkspacePaths } from '../../common/src/paths.mjs';
import { CodeGraphBackend, indexDirName, resolveIndexSelection } from '../src/backend.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const FAKE = path.join(HERE, 'fake-codegraph.mjs');

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
  const backend = new CodeGraphBackend({ paths, runtime: 'r', cliEntry: 'c', dirName: '.codegraph-atsf4g-test' });
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
    assert.equal(env.CODEGRAPH_DIR, '.codegraph-atsf4g-test');
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
      dirName: indexDirName(),
      needsInit: true,
    });
    fs.mkdirSync(path.join(paths.repoRoot, '.codegraph'), { recursive: true });
    fs.writeFileSync(path.join(paths.repoRoot, '.codegraph', 'codegraph.db'), '');
    assert.deepEqual(resolveIndexSelection(paths.repoRoot), { dirName: null, needsInit: false });
  } finally {
    fs.rmSync(paths.repoRoot, { recursive: true, force: true });
  }
});
