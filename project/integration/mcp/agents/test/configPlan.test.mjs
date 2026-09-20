import assert from 'node:assert/strict';
import path from 'node:path';
import test from 'node:test';

import { planConfigChanges } from '../src/configPlan.mjs';

// Pure planning with injectable I/O: a synthetic registry shares one physical
// file between two products (the .mcp.json group shape P9.3 will use).

const SHARED_TARGET = 'shared-file';
const SYNTHETIC_TARGETS = { [SHARED_TARGET]: { file: '.mcp.json', format: 'mcpServers', pathPolicy: 'absolute-args' } };
const SYNTHETIC_REGISTRY = [
  { id: 'alpha', label: 'Alpha', aliases: [], capability: 'auto', targetId: SHARED_TARGET },
  { id: 'beta', label: 'Beta', aliases: ['bee'], capability: 'auto', targetId: SHARED_TARGET },
];
const SHARED_FILE = path.join('/repo', '.mcp.json');

function makeReader(files) {
  const reads = [];
  return {
    reads,
    readFile: (file) => {
      reads.push(file);
      return Object.prototype.hasOwnProperty.call(files, file) ? files[file] : null;
    },
  };
}

test('operations on a shared physical file merge into one step', () => {
  const repo = '/repo';
  const reader = makeReader({});
  const plan = planConfigChanges({
    repoRoot: repo,
    operations: [
      { type: 'configure', agentId: 'alpha', backend: 'tgrep' },
      { type: 'configure', agentId: 'beta', backend: 'tgrep' },
    ],
    readFile: reader.readFile,
    registry: SYNTHETIC_REGISTRY,
    targets: SYNTHETIC_TARGETS,
  });
  assert.equal(plan.problems.length, 0);
  assert.equal(plan.steps.length, 1, 'one physical write for a shared file');
  assert.deepEqual(plan.steps[0].agents.sort(), ['alpha', 'beta']);
  assert.equal(reader.reads.length, 1, 'file read exactly once');
  assert.equal(plan.steps[0].action, 'create');
  assert.ok(plan.steps[0].after.includes('atsf4g-tgrep'));
});

test('aliases resolve to the same shared file', () => {
  const reader = makeReader({});
  const plan = planConfigChanges({
    repoRoot: '/repo',
    operations: [
      { type: 'configure', agentId: 'beta', backend: 'codegraph' },
      { type: 'configure', agentId: 'bee', backend: 'codegraph' },
    ],
    readFile: reader.readFile,
    registry: SYNTHETIC_REGISTRY,
    targets: SYNTHETIC_TARGETS,
  });
  assert.equal(plan.steps.length, 1);
  assert.deepEqual(plan.steps[0].agents.sort(), ['beta']);
});

test('conflicting backends for one file become a problem, not a step', () => {
  const reader = makeReader({});
  const plan = planConfigChanges({
    repoRoot: '/repo',
    operations: [
      { type: 'configure', agentId: 'alpha', backend: 'tgrep' },
      { type: 'configure', agentId: 'beta', backend: 'codegraph' },
    ],
    readFile: reader.readFile,
    registry: SYNTHETIC_REGISTRY,
    targets: SYNTHETIC_TARGETS,
  });
  assert.equal(plan.steps.length, 0);
  assert.equal(plan.problems.length, 1);
  assert.equal(plan.problems[0].error.kind, 'conflict');
});

test('remove-only on a missing file is a no-op step', () => {
  const reader = makeReader({});
  const plan = planConfigChanges({
    repoRoot: '/repo',
    operations: [{ type: 'remove', agentId: 'alpha' }],
    readFile: reader.readFile,
    registry: SYNTHETIC_REGISTRY,
    targets: SYNTHETIC_TARGETS,
  });
  assert.equal(plan.problems.length, 0);
  assert.equal(plan.steps[0].action, 'unchanged');
  assert.equal(plan.steps[0].deleteFile, false);
});

test('empty skeleton after removal deletes only with the ownership record', () => {
  const files = { [SHARED_FILE]: JSON.stringify({ mcpServers: { 'atsf4g-tgrep': {
    command: 'node', args: [path.join('/repo', 'project/integration/mcp/tgrep/src/server.mjs')],
  } } }) };
  for (const owns of [true, false]) {
    const reader = makeReader({ ...files });
    const plan = planConfigChanges({
      repoRoot: '/repo',
      operations: [{ type: 'remove', agentId: 'alpha' }],
      readFile: reader.readFile,
      ownsFile: () => owns,
      registry: SYNTHETIC_REGISTRY,
      targets: SYNTHETIC_TARGETS,
    });
    assert.equal(plan.steps[0].deleteFile, owns, `owns=${owns}`);
    assert.equal(plan.steps[0].action, owns ? 'delete' : 'update');
  }
});

test('damaged targets land in problems; healthy targets still plan', () => {
  const reader = makeReader({ [SHARED_FILE]: '{ broken' });
  const plan = planConfigChanges({
    repoRoot: '/repo',
    operations: [{ type: 'configure', agentId: 'alpha', backend: 'tgrep' }],
    readFile: reader.readFile,
    registry: SYNTHETIC_REGISTRY,
    targets: SYNTHETIC_TARGETS,
  });
  assert.equal(plan.steps.length, 0);
  assert.equal(plan.problems.length, 1);
  assert.equal(plan.problems[0].error.kind, 'invalid-json');
});

test('unknown agent ids throw before any file is read', () => {
  const reader = makeReader({});
  assert.throws(() =>
    planConfigChanges({
      repoRoot: '/repo',
      operations: [{ type: 'configure', agentId: 'ghost', backend: 'tgrep' }],
      readFile: reader.readFile,
      registry: SYNTHETIC_REGISTRY,
      targets: SYNTHETIC_TARGETS,
    }),
  );
  assert.equal(reader.reads.length, 0);
});
