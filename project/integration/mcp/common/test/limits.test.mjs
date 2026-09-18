import assert from 'node:assert/strict';
import test from 'node:test';

import { LIMITS, clampContext, clampResultCount, truncateRecords } from '../src/limits.mjs';

test('clampContext bounds context lines', () => {
  assert.equal(clampContext(undefined), 0);
  assert.equal(clampContext(2), 2);
  assert.equal(clampContext(99), LIMITS.maxContextLines);
  assert.equal(clampContext(-1), 0);
  assert.equal(clampContext('not-a-number'), 0);
});

test('clampResultCount bounds the row cap', () => {
  assert.equal(clampResultCount(undefined, 200), 200);
  assert.equal(clampResultCount(5, 200), 5);
  assert.equal(clampResultCount(0, 200), 1);
  assert.equal(clampResultCount(100000, 200), LIMITS.maxMatchRows);
});

test('truncateRecords cuts at whole-record boundaries', () => {
  const records = [{ n: 1 }, { n: 2 }, { n: 3 }];
  const result = truncateRecords(records, 2);
  assert.deepEqual(result.kept, [{ n: 1 }, { n: 2 }]);
  assert.equal(result.truncated, true);

  const exact = truncateRecords(records, 3);
  assert.equal(exact.truncated, false);
  assert.equal(exact.kept.length, 3);
});

test('truncateRecords enforces the byte budget', () => {
  const big = 'x'.repeat(LIMITS.maxResponseBytes);
  const result = truncateRecords([{ payload: big }, { payload: big }], 100);
  assert.equal(result.kept.length, 0);
  assert.equal(result.truncated, true);
});
