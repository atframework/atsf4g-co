/** Workspace scan policy participates in the same preflight/rollback as clients. */
import path from 'node:path';
import { isUnrealWorkspace, unrealCodegraphPolicy } from '../../../common/src/scanPolicy.mjs';
import { parseJsonDocument } from '../formats/jsonDocument.mjs';
import { AgentConfigError } from '../errors.mjs';
import { createRequire } from 'node:module';

const jsonc = createRequire(import.meta.url)('../../vendor/jsonc-parser/lib/umd/main.js');

export function planWorkspacePolicy({ repoRoot, readFile }) {
  if (!isUnrealWorkspace(repoRoot)) return [];
  const relative = 'codegraph.json';
  const file = path.join(repoRoot, relative);
  const before = readFile(file);
  const document = parseJsonDocument(before ?? '{}\n', file);
  let after = document.text;
  const eol = after.includes('\r\n') ? '\r\n' : '\n';
  for (const [key, defaults] of Object.entries(unrealCodegraphPolicy())) {
    const existing = document.root[key] ?? [];
    if (!Array.isArray(existing) || existing.some((value) => typeof value !== 'string' || !value.trim())) {
      throw new AgentConfigError(`${relative}: ${key} must be an array of nonempty patterns`, 'invalid-scan-policy');
    }
    // A present include list is the user's source selection. Never broaden it.
    const additions = key === 'include' && Object.hasOwn(document.root, key) ? [] : defaults;
    const merged = [...new Set([...existing, ...additions])];
    if (JSON.stringify(existing) !== JSON.stringify(merged)) {
      after = jsonc.applyEdits(after, jsonc.modify(after, [key], merged, { formattingOptions: { insertSpaces: true, tabSize: 2, eol } }));
    }
  }
  if (document.hadBom) after = '\uFEFF' + after;
  return [{
    target: { targetId: 'workspace-scan-policy' }, file, relative, agents: [], ops: [],
    before, after, action: before === after ? 'unchanged' : before === null ? 'create' : 'update', deleteFile: false,
  }];
}
