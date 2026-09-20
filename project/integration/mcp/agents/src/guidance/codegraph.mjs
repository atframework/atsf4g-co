/** Conditional CodeGraph guidance and byte-preserving Markdown block updates. */
import path from 'node:path';
import { TextDecoder } from 'node:util';
import { AgentConfigError } from '../errors.mjs';

export const CODEGRAPH_START = '<!-- CODEGRAPH_START -->';
export const CODEGRAPH_END = '<!-- CODEGRAPH_END -->';
export const CODEGRAPH_BODY = [
  '## CodeGraph',
  '',
  'After reading the applicable agent instructions, prefer CodeGraph before broad `rg`/file reads when locating',
  'symbols or understanding call relationships, if this repository already has an index (`codegraph.db` in',
  '`.codegraph/` or the appropriate platform-specific `.codegraph-*/` directory at the repository root).',
  '',
  '- **MCP:** Use `codegraph_explore` with symbol names, a file name, or a focused question. If the tool is deferred,',
  '  discover it by name through tool search when available. Results include source excerpts and structural',
  '  relationships; they can be incomplete or stale and do not prove complete C++ dynamic-dispatch semantics.',
  '  Verify relevant current source before making changes.',
  '- **Shell fallback:** When a matching CLI is already installed and the index is idle, run',
  '  `codegraph explore "<symbol names or question>"` from the repository root. Prefer this integration\'s prepared,',
  '  pinned CLI and the direct/offline environment described in `project/integration/mcp/README.md`.',
  '  Set `CODEGRAPH_DIR` to the actual root-level directory **name**, such as `.codegraph` or',
  '  `.codegraph-atsf4g-windows`; do not use an absolute path, a trailing slash, or another platform\'s index.',
  '  Do not use an unpinned `npx` command that may download a different release.',
  '',
  'If no usable index exists, skip CodeGraph. If it is unavailable, reports `INDEX_NOT_READY`/`INDEX_IN_USE`,',
  'or lacks the needed coverage, continue with `rg` and source reads. Do not initialize, rebuild, unlock,',
  'or download CodeGraph just to follow this guidance; indexing remains the user\'s decision.',
].join('\n');

function decode(bytes) {
  let encoding = 'utf-8';
  let bomLength = 0;
  if (bytes.subarray(0, 3).equals(Buffer.from([0xef, 0xbb, 0xbf]))) bomLength = 3;
  else if (bytes.subarray(0, 2).equals(Buffer.from([0xff, 0xfe]))) { encoding = 'utf-16le'; bomLength = 2; }
  else if (bytes.subarray(0, 2).equals(Buffer.from([0xfe, 0xff]))) { encoding = 'utf-16be'; bomLength = 2; }
  try {
    const text = new TextDecoder(encoding, { fatal: true, ignoreBOM: true }).decode(bytes.subarray(bomLength));
    // NULs usually indicate UTF-32 or BOM-less UTF-16. Do not guess an encoding.
    if (text.includes('\0')) throw new Error('NUL in Markdown');
    return { text, encoding, bom: bytes.subarray(0, bomLength) };
  } catch {
    throw new AgentConfigError('AGENTS.md: expected valid UTF-8 or BOM-marked UTF-16 LE/BE; file kept unchanged', 'guidance-encoding');
  }
}

function encode(text, { encoding, bom }) {
  const bytes = Buffer.from(text, encoding === 'utf-8' ? 'utf8' : 'utf16le');
  return Buffer.concat([bom, encoding === 'utf-16be' ? bytes.swap16() : bytes]);
}

/** Find standalone markers outside fenced examples, preserving source offsets. */
function markerPair(text) {
  const starts = [];
  const ends = [];
  let fence = null;
  for (const match of text.matchAll(/[^\r\n]*(?:\r\n|\r|\n)|[^\r\n]+$/g)) {
    const raw = match[0];
    const line = raw.replace(/(?:\r\n|\r|\n)$/, '');
    const fenceLine = line.match(/^ {0,3}(`{3,}|~{3,})(.*)$/);
    if (fence) {
      if (fenceLine && fenceLine[1][0] === fence.char && fenceLine[1].length >= fence.width && fenceLine[2].trim() === '') fence = null;
      continue;
    }
    if (fenceLine) { fence = { char: fenceLine[1][0], width: fenceLine[1].length }; continue; }
    const marker = { offset: match.index, next: match.index + raw.length, eol: raw.slice(line.length) };
    if (line.trim() === CODEGRAPH_START) starts.push(marker);
    if (line.trim() === CODEGRAPH_END) ends.push(marker);
  }
  const fail = () => { throw new AgentConfigError('AGENTS.md: duplicate, reversed or unclosed CodeGraph markers/fence; file kept unchanged', 'guidance-marker'); };
  if (fence) fail();
  if (starts.length === 0 && ends.length === 0) return null;
  if (starts.length !== 1 || ends.length !== 1 || starts[0].next > ends[0].offset) fail();
  return { start: starts[0], end: ends[0] };
}

export function patchCodegraphGuidance(before) {
  const document = decode(before ?? Buffer.alloc(0));
  const { text } = document;
  const markers = markerPair(text);
  const normalize = (value) => value.replace(/\r\n|\r/g, '\n');
  const expected = `${CODEGRAPH_BODY}\n`;
  if (markers && normalize(text.slice(markers.start.next, markers.end.offset)) === expected) {
    return { bytes: before, changed: false };
  }
  const eol = markers?.start.eol || text.match(/\r\n|\r|\n/)?.[0] || '\n';
  const body = expected.replace(/\n/g, eol);
  let after;
  if (markers) {
    after = text.slice(0, markers.start.next) + body + text.slice(markers.end.offset);
  } else {
    const separator = text === '' ? '' : /(?:\r\n|\r|\n)$/.test(text) ? eol : eol + eol;
    after = text + separator + CODEGRAPH_START + eol + body + CODEGRAPH_END + eol;
  }
  return { bytes: encode(after, document), changed: true };
}

export function planCodegraphGuidance({ repoRoot, readBuffer }) {
  const relative = 'AGENTS.md';
  const file = path.join(repoRoot, relative);
  const before = readBuffer(file);
  const result = patchCodegraphGuidance(before);
  return {
    target: { targetId: 'codegraph-guidance' }, relative, file, agents: [], ops: [],
    before, after: result.bytes, action: result.changed ? before === null ? 'create' : 'update' : 'unchanged',
    deleteFile: false, guidance: true,
  };
}
