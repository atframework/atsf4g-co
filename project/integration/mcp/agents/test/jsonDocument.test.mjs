import assert from 'node:assert/strict';
import test from 'node:test';

import {
  documentIsEmptySkeleton,
  parseJsonDocument,
  removeServerEntry,
  upsertServerEntry,
  walkServerMap,
} from '../src/formats/jsonDocument.mjs';

const ENTRY = { type: 'stdio', command: 'node', args: ['/repo/tgrep/server.mjs'] };

test('parseJsonDocument accepts JSON and JSONC, rejects damage', () => {
  assert.deepEqual(parseJsonDocument('{"a":1}', 'f.json').root, { a: 1 });
  assert.deepEqual(parseJsonDocument('{ // c\n"a":1 }', 'f.json').root, { a: 1 });
  assert.deepEqual(parseJsonDocument('{"a":1,}', 'f.json').root, { a: 1 }, 'trailing comma allowed');
  assert.equal(parseJsonDocument('\uFEFF{"a":1}', 'f.json').hadBom, true);

  for (const [text, kind] of [
    ['{ broken', 'invalid-json'],
    ['', 'invalid-json'],
    ['\n', 'invalid-json'],
    ['[1,2]', 'not-object'],
    ['"x"', 'not-object'],
    ['{"a":1,"a":2}', 'duplicate-key'],
  ]) {
    assert.throws(() => parseJsonDocument(text, 'f.json'), (error) => error.kind === kind, `${text} → ${kind}`);
  }
});

test('duplicate-key detection ignores comment false positives', () => {
  const text = '{\n  // "mcpServers": looks like a key but lives in a comment\n  "mcpServers": {}\n}\n';
  const document = parseJsonDocument(text, 'f.json');
  assert.deepEqual(document.root, { mcpServers: {} });
});

test('upsertServerEntry creates containers for every format root path', () => {
  const cases = [
    ['mcpServers', '{"model":"x"}\n'],
    ['vscodeServers', '{}\n'],
    ['zcode', '{}\n'],
    ['opencode', '{}\n'],
  ];
  for (const [format, seed] of cases) {
    const edited = upsertServerEntry(seed, format, 'atsf4g-tgrep', ENTRY);
    const document = parseJsonDocument(edited, 'f.json');
    const map = walkServerMap(document.root, format, 'f.json');
    assert.ok(map, `${format} containers created`);
    assert.deepEqual(map['atsf4g-tgrep'], ENTRY, `${format} entry written`);
  }
});

test('removeServerEntry restores the original bytes exactly', () => {
  const original = '{\n  // keep me\n  "mcpServers": {\n    "user": { "command": "x" }\n  }\n}\n';
  const edited = upsertServerEntry(original, 'mcpServers', 'atsf4g-tgrep', ENTRY);
  assert.notEqual(edited, original);
  assert.equal(removeServerEntry(edited, 'mcpServers', 'atsf4g-tgrep'), original);
});

test('edits preserve CRLF line endings', () => {
  const original = '{\r\n  "mcpServers": {\r\n    "user": { "command": "x" }\r\n  }\r\n}\r\n';
  const edited = upsertServerEntry(original, 'mcpServers', 'atsf4g-tgrep', ENTRY);
  assert.ok(edited.includes('\r\n'), 'CRLF kept');
  assert.doesNotMatch(edited.replace(/\r\n/g, ''), /(?<!\r)\n/, 'no bare LF introduced');
  const document = parseJsonDocument(edited, 'f.json');
  assert.ok(document.root.mcpServers['atsf4g-tgrep']);
});

test('walkServerMap refuses non-object containers', () => {
  for (const [root, format] of [
    [{ mcpServers: [] }, 'mcpServers'],
    [{ mcpServers: null }, 'mcpServers'],
    [{ mcpServers: 'x' }, 'mcpServers'],
    [{ mcp: { servers: [] } }, 'zcode'],
    [{ mcp: [] }, 'zcode'],
    [{ mcp: 'x' }, 'opencode'],
    [{ servers: [] }, 'vscodeServers'],
  ]) {
    assert.throws(() => walkServerMap(root, format, 'f.json'), (error) => error.kind === 'map-not-object');
  }
  assert.equal(walkServerMap({ model: 'x' }, 'mcpServers', 'f.json'), null, 'absent container is fine');
});

test('documentIsEmptySkeleton recognizes only comment-free managed-only documents', () => {
  assert.equal(documentIsEmptySkeleton('{}', 'mcpServers'), true);
  assert.equal(documentIsEmptySkeleton('{\n  "mcpServers": {}\n}\n', 'mcpServers'), true);
  assert.equal(documentIsEmptySkeleton('{"mcp":{"servers":{}}}', 'zcode'), true);
  assert.equal(documentIsEmptySkeleton('{"mcp":{}}', 'zcode'), true, 'missing servers container after removal');
  assert.equal(documentIsEmptySkeleton('{ /* comment */ }', 'mcpServers'), false, 'comments count as user content');
  assert.equal(documentIsEmptySkeleton('{\n  // c\n  "mcpServers": {}\n}\n', 'mcpServers'), false);
  assert.equal(documentIsEmptySkeleton('{"theme":"dark"}', 'mcpServers'), false, 'foreign keys count as user content');
  assert.equal(documentIsEmptySkeleton('{"mcpServers":{},"theme":"dark"}', 'mcpServers'), false);
});
