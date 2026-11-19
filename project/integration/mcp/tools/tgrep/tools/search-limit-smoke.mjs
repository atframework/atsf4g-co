/** Real stdio regression for search replies that exceed the backend's byte budget. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';

import { LineJsonRpcClient } from '../../../common/src/lineRpc.mjs';
import { LIMITS } from '../../../common/src/limits.mjs';
import { minimalEnvironment, StderrSink, supervise } from '../../../common/src/supervisor.mjs';

if (process.argv.length !== 4) throw new Error('usage: node search-limit-smoke.mjs <tgrep-binary> <scratch-dir>');
const binary = path.resolve(process.argv[2]);
const scratch = path.resolve(process.argv[3]);
fs.mkdirSync(scratch, { recursive: true });
const fixture = fs.mkdtempSync(path.join(scratch, 'tgrep-search-limit-'));
const root = path.join(fixture, 'project');
fs.mkdirSync(path.join(root, '.git'), { recursive: true });
fs.mkdirSync(path.join(root, 'many'));
fs.mkdirSync(path.join(root, 'single'));

const small = 'needle_small\n';
const line = 'needle_broad ' + 'x'.repeat(1024) + '\n';
const files = 300;
const rowsPerFile = 32;
const singleFileRows = files * rowsPerFile;
fs.writeFileSync(path.join(root, 'small.txt'), small);
for (let file = 0; file < files; file++) {
  fs.writeFileSync(path.join(root, 'many', `file-${file}.txt`), line.repeat(rowsPerFile));
}
fs.writeFileSync(path.join(root, 'single', 'large.txt'), line.repeat(singleFileRows));
assert.ok(Buffer.byteLength(line) * singleFileRows > LIMITS.maxBackendFrameBytes);

const stderrPath = path.join(fixture, 'stderr.log');
const index = path.join(fixture, 'index');
const proc = supervise({
  argv: [binary, 'serve', root, '--transport', 'stdio', '--index-path', index, '--no-watch'],
  cwd: root,
  env: minimalEnvironment(),
  label: 'tgrep-search-limit',
  stderrSink: new StderrSink({ filePath: stderrPath }),
});
const rpc = new LineJsonRpcClient(proc);
const expected = { detail: true, fixed_string: true, pattern: 'needle_broad' };
try {
  let status;
  const deadline = Date.now() + 30_000;
  do {
    status = await rpc.request('status', {});
    if (!status.indexing && status.hidden_complete) break;
    await delay(50);
  } while (Date.now() < deadline);
  assert.equal(status.indexing, false);
  assert.equal(status.hidden_complete, true);
  for (const scope of ['many', 'single']) {
    const result = await rpc.request('search', { ...expected, scope });
    assert.equal(result.truncated, true, scope);
    assert.equal(result.num_matches, result.matches.length, scope);
    assert.ok(result.num_matches >= LIMITS.maxMatchRows && result.num_matches < singleFileRows, scope);
    assert.ok(result.matches.every(row => row.content.includes('needle_broad')), scope);
    assert.equal(new Set(result.matches.map(row => `${row.file}:${row.line}`)).size, result.num_matches, scope);
    assert.ok(Buffer.byteLength(JSON.stringify(result)) < LIMITS.maxBackendFrameBytes, scope);
  }
  const result = await rpc.request('search', { ...expected, pattern: small.trim() });
  assert.equal(result.num_matches, 1);
  assert.equal(result.truncated, false);
  assert.equal(result.matches[0].file, 'small.txt');
  assert.equal(fs.existsSync(path.join(index, 'serve.json')), false);
  console.log(`Broad and small searches passed: ${fixture}`);
} finally {
  await proc.stop();
  assert.equal(proc.alive(), false);
}

