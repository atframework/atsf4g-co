/** Codex TOML editing over shared lexical boundaries. */
import { AgentConfigError } from '../errors.mjs';
import { BACKENDS } from '../registry.mjs';
import { isOurServerEntry } from '../entries.mjs';
import { scanDocument, TOML_BEGIN, TOML_END, parseTableHeader } from './tomlDocument.mjs';
export { TOML_BEGIN, TOML_END, parseTableHeader, tomlMultilineAfter } from './tomlDocument.mjs';

function tomlQuote(value) {
  return JSON.stringify(String(value)).replace(/\u007f/g, '\\u007f');
}

export function codexTomlSection(repoRoot, backend) {
  return [TOML_BEGIN, `[mcp_servers.${BACKENDS[backend].serverId}]`, 'command = "node"',
    `args = [${tomlQuote(BACKENDS[backend].entry.split(/[\\/]+/).join('/'))}]`,
    `cwd = ${tomlQuote(repoRoot)}`, TOML_END].join('\n');
}

export function removeCodexTomlServers(text) {
  const { records } = scanDocument(text);
  return { text: records.filter((r) => !r.remove).map((r) => r.bytes).join(''), changed: records.some((r) => r.remove) };
}

export function codexManagedTables(text) {
  return scanDocument(text).found;
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

/** Validate the command/path before claiming an unmarked or edited server table. */
export function assertCodexServers(text, repoRoot) {
  const { records, found } = scanDocument(text);
  for (const id of found) {
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
    const array = fields.get('args');
    const args = /^\s*\[/.test(array ?? '') ? array.replace(/^\s*\[\s*(?:#[^\n]*(?:\n|$)\s*)*/, '') : undefined;
    const entry = { command: stringPrefix(fields.get('command')), args: [stringPrefix(args)], cwd: repoRoot };
    if (fields.has('cwd')) {
      entry.cwd = stringPrefix(fields.get('cwd'));
      if (entry.cwd === undefined) throw new AgentConfigError(`${id}: unsupported cwd expression`, 'server-conflict');
    }
    if (!isOurServerEntry(entry, repoRoot)) throw new AgentConfigError(`${id}: command/args do not point at this repository's wrapper`, 'server-conflict');
  }
}

/** Keep optional fields and subtables when updating the selected server. */
export function configureCodexToml(text, repoRoot, backend) {
  const original = text ?? '';
  const { records, found } = scanDocument(original);
  const selected = BACKENDS[backend].serverId;
  const eol = original.includes('\r\n') ? '\r\n' : '\n';
  if (!found.has(selected) || found.size !== 1) {
    let base = removeCodexTomlServers(original).text;
    if (base && !base.endsWith('\n')) base += eol;
    return base + codexTomlSection(repoRoot, backend).replace(/\n/g, eol) + eol;
  }
  const fields = new Map([
    ['cwd', tomlQuote(repoRoot)],
  ]);
  const start = records.findIndex((r) => r.header && r.table?.length === 2 && r.table[1] === selected);
  if (start < 0) throw new AgentConfigError('managed Codex server has only subtables; repair its parent table first', 'toml-syntax');
  let end = start + 1;
  while (end < records.length && !records[end].header && records[end].bytes.trim() !== TOML_END) end++;
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
