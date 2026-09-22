/** Codex TOML editing over shared lexical boundaries. */
import { AgentConfigError } from '../errors.mjs';
import { BACKENDS, managedServerIds, backendForServerId } from '../backends.mjs';
import { isOurServerEntry, serverEntry, entryExtraArgs } from '../entries.mjs';
import { scanDocument, TOML_BEGIN, TOML_END, parseTableHeader } from './tomlDocument.mjs';
export { TOML_BEGIN, TOML_END, parseTableHeader, tomlMultilineAfter } from './tomlDocument.mjs';

function tomlQuote(value) {
  return JSON.stringify(String(value)).replace(/\u007f/g, '\\u007f');
}

export function codexTomlSection(repoRoot, backend, launch = {}) {
  const entry = serverEntry('mcpServersCwd', repoRoot, backend, launch);
  return [TOML_BEGIN, `[mcp_servers.${BACKENDS[backend].serverId}]`, `command = ${tomlQuote(entry.command)}`,
    `args = [${entry.args.map(tomlQuote).join(', ')}]`,
    `cwd = ${tomlQuote(repoRoot)}`, 'enabled = true', TOML_END].join('\n');
}

export function removeCodexTomlServers(text) {
  const { records } = scanDocument(text);
  return { text: records.filter((r) => !r.remove).map((r) => r.bytes).join(''), changed: records.some((r) => r.remove) };
}

export function codexManagedTables(text, repoRoot, launch = {}) {
  return scanDocument(repoRoot ? normalizeCodexServers(text, repoRoot, launch) : text).found;
}

function assignment(record) {
  if (!record.valueStart) return null;
  const match = record.bytes.match(/^\s*((?:[A-Za-z0-9_-]+)|(?:"(?:\\.|[^"\\])*"|'[^']*'))\s*=\s*([\s\S]*)$/);
  if (!match) return null;
  const key = parseTableHeader(`[${match[1]}]`)?.[0];
  return { key, value: match[2] };
}

function stringPrefix(value) {
  const match = value?.trimStart().match(/^("(?:\\.|[^"\\])*"|'[^']*')/);
  return match ? parseTableHeader(`[${match[1]}]`)?.[0] : undefined;
}

/** Parse literal string args without evaluating TOML expressions or rewriting it. */
function argumentStrings(value) {
  if (!/^\s*\[/.test(value ?? '')) return [];
  let remaining = value.replace(/^\s*\[/, '');
  const args = [];
  while (true) {
    remaining = remaining.replace(/^(?:\s|#[^\r\n]*(?:\r?\n|$))*/, '');
    if (remaining.startsWith(']')) return args;
    const token = remaining.match(/^("(?:\\.|[^"\\])*"|'[^']*')/);
    if (!token) throw new AgentConfigError('managed TOML args must be literal strings', 'toml-syntax');
    args.push(stringPrefix(token[0]));
    remaining = remaining.slice(token[0].length).replace(/^(?:\s|#[^\r\n]*(?:\r?\n|$))*/, '');
    if (remaining.startsWith(']')) return args;
    if (!remaining.startsWith(',')) throw new AgentConfigError('managed TOML args must be a string array', 'toml-syntax');
    remaining = remaining.slice(1);
  }
}

/** Validate the command/path before claiming an unmarked or edited server table. */
function entryFromRecords(records, id, repoRoot) {
    const fields = new Map();
    for (let i = 0; i < records.length; i++) {
      const r = records[i];
      if (r.table?.length !== 2 || r.table[1] !== id) continue;
      const field = assignment(r);
      if (!field) continue;
      if (fields.has(field.key)) throw new AgentConfigError(`${id}: duplicate TOML field`, 'toml-syntax');
      let value = field.value;
      for (let j = i + 1; j < records.length && !records[j].valueStart && !records[j].header && records[j].table === r.table; j++) value += records[j].bytes;
      fields.set(field.key, value);
    }
    const entry = { command: stringPrefix(fields.get('command')), args: argumentStrings(fields.get('args')), cwd: repoRoot };
    if (fields.has('cwd')) {
      entry.cwd = stringPrefix(fields.get('cwd'));
      if (entry.cwd === undefined) throw new AgentConfigError(`${id}: unsupported cwd expression`, 'server-conflict');
    }
    return entry;
}

/** Change verified legacy table keys/markers only; retain options and string values. */
function normalizeCodexServers(text, repoRoot, launch) {
  const { records, found } = scanDocument(text, { legacyCandidates: true });
  const renames = new Map();
  for (const id of found) {
    if (managedServerIds().includes(id)) continue;
    if (!isOurServerEntry(entryFromRecords(records, id, repoRoot), repoRoot, launch, backendForServerId(id))) continue;
    const next = BACKENDS[backendForServerId(id)].serverId;
    if (found.has(next) || [...renames.values()].includes(next)) throw new AgentConfigError(`${id} and ${next} both exist; merge their settings before migrating`, 'server-conflict');
    renames.set(id, next);
  }
  return records.map(record => {
    if (record.marker) return (record.marker === 'begin' ? TOML_BEGIN : TOML_END) + (record.bytes.match(/\r?\n$/)?.[0] ?? '');
    if (!record.header || record.table?.[0] !== 'mcp_servers' || !renames.has(record.table[1])) return record.bytes;
    return record.bytes.replace(/^(\s*\[+\s*(?:mcp_servers|"mcp_servers"|'mcp_servers')\s*\.\s*)(?:"(?:\\.|[^"\\])*"|'[^']*'|[A-Za-z0-9_-]+)/,
      (_, prefix) => prefix + renames.get(record.table[1]));
  }).join('');
}

export function assertCodexServers(text, repoRoot, launch = {}) {
  const normalized = normalizeCodexServers(text, repoRoot, launch);
  const { records, found } = scanDocument(normalized);
  for (const id of found) {
    if (!isOurServerEntry(entryFromRecords(records, id, repoRoot), repoRoot, launch)) throw new AgentConfigError(`${id}: command/args do not point at this repository's wrapper`, 'server-conflict');
  }
  return normalized;
}

/** Keep optional fields and subtables when updating the selected server. */
export function configureCodexToml(text, repoRoot, backend, launch = {}) {
  const original = text ?? '';
  const { records, found } = scanDocument(original);
  const selected = BACKENDS[backend].serverId;
  const eol = original.includes('\r\n') ? '\r\n' : '\n';
  if (!found.has(selected) || found.size !== 1) {
    let base = removeCodexTomlServers(original).text;
    if (base && !base.endsWith('\n')) base += eol;
    return base + codexTomlSection(repoRoot, backend, launch).replace(/\n/g, eol) + eol;
  }
  const fields = new Map([
    ['cwd', tomlQuote(repoRoot)],
    ['enabled', 'true'],
  ]);
  const start = records.findIndex((r) => r.header && r.table?.length === 2 && r.table[1] === selected);
  if (start < 0) throw new AgentConfigError('managed Codex server has only subtables; repair its parent table first', 'toml-syntax');
  let end = start + 1;
  while (end < records.length && !records[end].header && records[end].bytes.trim() !== TOML_END) end++;
  const oldEntry = entryFromRecords(records, selected, repoRoot);
  const nextEntry = serverEntry('mcpServersCwd', repoRoot, backend, launch);
  nextEntry.args.push(...entryExtraArgs(oldEntry, repoRoot, launch));
  // Update launch fields even when the backend stays selected. Keep comments
  // from a multiline args value as standalone comments, and leave other fields
  // and subtables byte-for-byte intact.
  const replacements = new Map();
  if (oldEntry.command !== nextEntry.command) replacements.set('command', tomlQuote(nextEntry.command));
  if (JSON.stringify(oldEntry.args) !== JSON.stringify(nextEntry.args)) replacements.set('args', `[${nextEntry.args.map(tomlQuote).join(', ')}]`);
  for (let i = start + 1; i < end; i++) {
    const field = assignment(records[i]);
    if (!field || !replacements.has(field.key)) continue;
    let last = i + 1;
    while (last < end && !records[last].valueStart && !records[last].header) last++;
    const raw = records.slice(i, last).map(record => record.bytes).join('');
    const prefix = raw.match(/^\s*(?:[A-Za-z0-9_-]+|"(?:\\.|[^"\\])*"|'[^']*')\s*=\s*/)[0];
    if (field.key === 'command') {
      records[i].bytes = raw.replace(/^(\s*(?:[A-Za-z0-9_-]+|"(?:\\.|[^"\\])*"|'[^']*')\s*=\s*)(?:"(?:\\.|[^"\\])*"|'[^']*')/,
        (_, before) => before + replacements.get(field.key));
    } else {
      const comments = (raw.slice(prefix.length).match(/"(?:\\.|[^"\\])*"|'[^']*'|#[^\r\n]*/g) ?? []).filter(token => token.startsWith('#'));
      records[i].bytes = comments.map(comment => comment + eol).join('') + prefix + replacements.get(field.key) + (raw.endsWith('\n') ? eol : '');
    }
    for (let j = i + 1; j < last; j++) records[j].bytes = '';
  }
  for (let i = start + 1; i < end; i++) {
    const record = records[i];
    if (!record.valueStart) continue;
    const field = assignment(record);
    if (!field || !fields.has(field.key)) continue;
    fields.delete(field.key);
  }
  const missing = [...fields].map(([key, value]) => `${key} = ${value}${eol}`).join('');
  if (missing && end > 0 && !records[end - 1].bytes.endsWith('\n')) records[end - 1].bytes += eol;
  records.splice(end, 0, { bytes: missing });
  return records.map((r) => r.bytes).join('');
}
