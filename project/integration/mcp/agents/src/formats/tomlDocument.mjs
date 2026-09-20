/** Conservative lexical boundaries for surgical TOML edits; not a full TOML validator. */
import { AgentConfigError } from '../errors.mjs';
import { managedServerIds } from '../registry.mjs';

export const TOML_BEGIN = '# BEGIN atsf4g-mcp (managed by project/integration/mcp/setup.js; keep the marker lines)';
export const TOML_END = '# END atsf4g-mcp';
const MANAGED_IDS = new Set(managedServerIds());

function basicKey(text) {
  return text.replace(/\\(u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|[\s\S])/g, (_, escape) => {
    if (/^[uU][0-9a-fA-F]+$/.test(escape)) {
      const point = Number.parseInt(escape.slice(1), 16);
      if (point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) throw new Error('invalid Unicode key');
      return String.fromCodePoint(point);
    }
    const decoded = { b: '\b', t: '\t', n: '\n', f: '\f', r: '\r', '"': '"', '\\': '\\' }[escape];
    if (decoded === undefined) throw new Error('invalid escape');
    return decoded;
  });
}

/** Parse headers, ignoring brackets/dots inside quoted keys and comments. */
export function parseTableHeader(line) {
  const text = line.trim();
  if (!text.startsWith('[')) return null;
  const width = text.startsWith('[[') ? 2 : 1;
  const segments = [];
  let i = width;
  const space = () => { while (i < text.length && /[\t ]/.test(text[i])) i++; };
  while (i < text.length) {
    space();
    const quote = text[i];
    if (quote === '"' || quote === "'") {
      const start = ++i;
      while (i < text.length && text[i] !== quote) {
        if (quote === '"' && text[i] === '\\') i++;
        i++;
      }
      if (i >= text.length) return null;
      const raw = text.slice(start, i++);
      try { segments.push(quote === '"' ? basicKey(raw) : raw); } catch { return null; }
    } else {
      const key = text.slice(i).match(/^[A-Za-z0-9_-]+/)?.[0];
      if (!key) return null;
      segments.push(key);
      i += key.length;
    }
    space();
    if (text[i] === '.') { i++; continue; }
    if (!text.startsWith(']'.repeat(width), i)) return null;
    i += width;
    return /^\s*(?:#.*)?$/.test(text.slice(i)) ? segments : null;
  }
  return null;
}

/** Strings and composite values can contain lines resembling table headers. */
function scanValue(line, state) {
  let { multiline, depth } = state;
  let i = 0;
  while (i < line.length) {
    if (multiline) {
      if (multiline === '"""' && line[i] === '\\') { i += 2; continue; }
      if (line.startsWith(multiline, i)) {
        const quote = multiline[0];
        i += 3;
        // Four/five closing quotes include literal quotes in the value.
        while (line[i] === quote) i++;
        multiline = null;
      } else i++;
      continue;
    }
    const char = line[i];
    if (char === '#') break;
    if (char === '"' || char === "'") {
      if (line.startsWith(char.repeat(3), i)) { multiline = char.repeat(3); i += 3; continue; }
      i++;
      while (i < line.length && line[i] !== char) {
        if (char === '"' && line[i] === '\\') i++;
        i++;
      }
      i++;
      continue;
    }
    if (char === '[' || char === '{') depth++;
    if (char === ']' || char === '}') depth--;
    i++;
  }
  return { multiline, depth };
}

export function tomlMultilineAfter(line, multiline) {
  return scanValue(line, { multiline, depth: 0 }).multiline;
}

function assignmentKeys(line) {
  let quote = null;
  for (let i = 0; i < line.length; i++) {
    const char = line[i];
    if (quote) {
      if (quote === '"' && char === '\\') i++;
      else if (char === quote) quote = null;
      continue;
    }
    if (char === '#') return null;
    if (char === '"' || char === "'") quote = char;
    else if (char === '=') return parseTableHeader(`[${line.slice(0, i).trim()}]`);
  }
  return null;
}

export function scanDocument(text) {
  const records = [];
  const found = new Set();
  let state = { multiline: null, depth: 0 };
  let marked = false;
  let table = null;
  let managed = false;
  const fail = (message, kind = 'toml-marker') => { throw new AgentConfigError(`.codex/config.toml: ${message}`, kind); };
  for (const bytes of text.match(/[^\n]*\n|[^\n]+$/g) ?? []) {
    const line = bytes.replace(/\r?\n$/, '');
    if (!state.multiline && state.depth === 0) {
      if (line.trim() === TOML_BEGIN) {
        if (marked) fail('BEGIN marker inside another managed block (overlapping markers)');
        marked = true;
        table = null;
        managed = false;
        records.push({ bytes, remove: true });
        continue;
      }
      if (line.trim() === TOML_END) {
        if (!marked) fail('END marker without BEGIN');
        marked = false;
        table = null;
        managed = false;
        records.push({ bytes, remove: true });
        continue;
      }
      const header = parseTableHeader(line);
      if (header) {
        if (header[0] === 'mcp_servers' && MANAGED_IDS.has(header[1]) && records.some((r) => r.header && JSON.stringify(r.table) === JSON.stringify(header))) {
          fail('duplicate managed table', 'toml-syntax');
        }
        table = header;
        managed = header[0] === 'mcp_servers' && MANAGED_IDS.has(header[1]);
        if (marked && !managed) fail('foreign table inside managed markers; move it outside before editing');
        if (managed) found.add(header[1]);
        records.push({ bytes, remove: managed || marked, table, header: true });
        continue;
      }
      if (marked && !table && line.trim() !== '' && !line.trim().startsWith('#')) fail('non-table content inside managed markers');
      // Inline/dotted server definitions need a full TOML tree editor. Refuse
      // these representations instead of appending a duplicate table.
      const keys = assignmentKeys(line);
      const fullPath = [...(table ?? []), ...(keys ?? [])];
      if ((!table || table.length < 2) && fullPath[0] === 'mcp_servers'
        && (fullPath.length === 1 || MANAGED_IDS.has(fullPath[1]))) {
        fail('inline/dotted managed server definitions require manual migration to tables', 'toml-shape');
      }
    }
    records.push({ bytes, remove: managed || marked, table, valueStart: !state.multiline && state.depth === 0 });
    state = scanValue(line, state);
  }
  if (marked) fail('BEGIN marker without END (unterminated managed block)');
  if (state.multiline || state.depth !== 0) fail('unterminated string or composite value', 'toml-syntax');
  return { records, found };
}
