import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { CODEGRAPH_BODY, CODEGRAPH_START, CODEGRAPH_END, patchCodegraphGuidance } from '../src/guidance/codegraph.mjs';
import { planAgentConfigChanges, applyAgentConfigChanges, runAgentConfigBatch } from '../src/writers.mjs';

const block = (eol = '\n') => `${CODEGRAPH_START}${eol}${CODEGRAPH_BODY.replace(/\n/g, eol)}${eol}${CODEGRAPH_END}`;
function encoded(text, format) {
  if (format === 'utf8') return Buffer.from(text);
  if (format === 'utf8bom') return Buffer.concat([Buffer.from([0xef, 0xbb, 0xbf]), Buffer.from(text)]);
  const bytes = Buffer.from(text, 'utf16le');
  return format === 'utf16le' ? Buffer.concat([Buffer.from([0xff, 0xfe]), bytes])
    : Buffer.concat([Buffer.from([0xfe, 0xff]), bytes.swap16()]);
}
function workspace(t) {
  const repoRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'mcp-guidance-'));
  t.after(() => fs.rmSync(repoRoot, { recursive: true, force: true }));
  return { repoRoot, stateDir: path.join(repoRoot, 'state'), tmpDir: path.join(repoRoot, 'tmp') };
}

test('equivalent guidance does not rewrite bytes across supported encodings and line endings', () => {
  for (const format of ['utf8', 'utf8bom', 'utf16le', 'utf16be']) {
    for (const eol of ['\n', '\r\n', '\r']) {
      const before = encoded(`用户规则${eol}${block(eol)}${eol}尾部规则${eol}`, format);
      const result = patchCodegraphGuidance(before);
      assert.equal(result.changed, false, `${format}/${JSON.stringify(eol)}`);
      assert.strictEqual(result.bytes, before, 'no re-encoding for equal content');
    }
  }
});

test('replace only the marked body and retain outside bytes, BOM and newline convention', () => {
  for (const format of ['utf8', 'utf8bom', 'utf16le', 'utf16be']) {
    const before = encoded(`前缀\r\n${CODEGRAPH_START}\r\nold body\r\n${CODEGRAPH_END}\r\n后缀\r\n`, format);
    const after = patchCodegraphGuidance(before);
    assert.equal(after.changed, true);
    assert.deepEqual(after.bytes, encoded(`前缀\r\n${block('\r\n')}\r\n后缀\r\n`, format));
  }
});

test('missing block appends once; marker examples inside Markdown fences are left alone', () => {
  const before = Buffer.from(`# Guide\n\n\x60\x60\x60markdown\n${CODEGRAPH_START}\nsample\n${CODEGRAPH_END}\n\x60\x60\x60\n`);
  const first = patchCodegraphGuidance(before);
  assert.ok(first.bytes.subarray(0, before.length).equals(before));
  assert.match(first.bytes.toString(), /sample/);
  assert.equal(patchCodegraphGuidance(first.bytes).changed, false);
  assert.equal(patchCodegraphGuidance(null).bytes.toString(), `${block()}\n`);
});

test('invalid or ambiguous markers and invalid encodings are rejected', () => {
  for (const text of [CODEGRAPH_START, CODEGRAPH_END, `${CODEGRAPH_END}\n${CODEGRAPH_START}`, `${block()}\n${block()}`,
    `${CODEGRAPH_START}\n${CODEGRAPH_START}\n${CODEGRAPH_END}`, 'unterminated fence\n\x60\x60\x60\n']) {
    assert.throws(() => patchCodegraphGuidance(Buffer.from(text)), (e) => e.kind === 'guidance-marker');
  }
  for (const bytes of [Buffer.from([0xff, 0x80]), Buffer.from([0xff, 0xfe, 0x41]), Buffer.from([0xff, 0xfe, 0x00, 0xd8])]) {
    assert.throws(() => patchCodegraphGuidance(bytes), (e) => e.kind === 'guidance-encoding');
  }
});

test('unchanged guidance retains mtime and creates no metadata or backups', (t) => {
  const options = workspace(t);
  const file = path.join(options.repoRoot, 'AGENTS.md');
  const before = encoded(block('\r\n') + '\r\n', 'utf16le');
  fs.writeFileSync(file, before);
  fs.utimesSync(file, new Date(1000000), new Date(1000000));
  const mtime = fs.statSync(file).mtimeMs;
  const result = runAgentConfigBatch({ ...options, operations: [], codegraphGuidance: true });
  assert.equal(result.plan.steps[0].action, 'unchanged');
  assert.deepEqual(fs.readFileSync(file), before);
  assert.equal(fs.statSync(file).mtimeMs, mtime);
  assert.deepEqual(fs.readdirSync(options.repoRoot), ['AGENTS.md']);
});

test('malformed guidance aborts config writes, and damaged configs abort guidance writes', (t) => {
  const options = workspace(t);
  const file = path.join(options.repoRoot, 'AGENTS.md');
  fs.writeFileSync(file, CODEGRAPH_START);
  const operations = [{ type: 'configure', agentId: 'claude', backend: 'codegraph' }];
  const result = runAgentConfigBatch({ ...options, operations, codegraphGuidance: true });
  assert.equal(result.applied, false);
  assert.equal(result.plan.problems[0].error.kind, 'guidance-marker');
  assert.equal(fs.existsSync(path.join(options.repoRoot, '.mcp.json')), false);
  fs.writeFileSync(file, 'user rules');
  fs.writeFileSync(path.join(options.repoRoot, '.mcp.json'), '{ broken');
  assert.equal(runAgentConfigBatch({ ...options, operations, codegraphGuidance: true }).applied, false);
  assert.equal(fs.readFileSync(file, 'utf8'), 'user rules');
});

test('a post-write failure restores the original encoded guidance and earlier agent configs', (t) => {
  const options = workspace(t);
  const file = path.join(options.repoRoot, 'AGENTS.md');
  const before = encoded(`中文\r\n${CODEGRAPH_START}\r\nold\r\n${CODEGRAPH_END}\r\n`, 'utf16be');
  fs.writeFileSync(file, before);
  const append = fs.appendFileSync;
  t.mock.method(fs, 'appendFileSync', (name, text, config) => {
    if (String(text).includes('"action":"updated"') && String(text).includes('AGENTS.md')) throw new Error('injected journal failure');
    return append(name, text, config);
  });
  const plan = planAgentConfigChanges({ ...options, operations: [{ type: 'configure', agentId: 'claude', backend: 'codegraph' }], codegraphGuidance: true });
  assert.throws(() => applyAgentConfigChanges({ ...options, plan }), (error) => {
    assert.deepEqual(error.rollback.map((entry) => entry.restored), [true, true]);
    return true;
  });
  assert.deepEqual(fs.readFileSync(file), before);
  assert.equal(fs.existsSync(path.join(options.repoRoot, '.mcp.json')), false);
});

test('concurrent re-encoding after planning is preserved and earlier config writes are rolled back', (t) => {
  const options = workspace(t);
  const file = path.join(options.repoRoot, 'AGENTS.md');
  const originalText = `用户规则\n${CODEGRAPH_START}\nold\n${CODEGRAPH_END}\n`;
  fs.writeFileSync(file, encoded(originalText, 'utf16le'));
  const plan = planAgentConfigChanges({ ...options, operations: [{ type: 'configure', agentId: 'claude', backend: 'codegraph' }], codegraphGuidance: true });
  const concurrent = encoded(originalText, 'utf8bom');
  fs.writeFileSync(file, concurrent);
  assert.throws(() => applyAgentConfigChanges({ ...options, plan }), (error) => {
    assert.equal(error.cause.kind, 'concurrent-modification');
    assert.deepEqual(error.rollback.map((entry) => [entry.relative, entry.restored]), [['.mcp.json', true]]);
    return true;
  });
  assert.deepEqual(fs.readFileSync(file), concurrent);
  assert.equal(fs.existsSync(path.join(options.repoRoot, '.mcp.json')), false);
});
