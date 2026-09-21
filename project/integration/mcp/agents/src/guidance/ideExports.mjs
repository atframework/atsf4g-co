/**
 * Guided-import delivery for IDE clients without a verified project-level
 * write path (Cline IDE, CodeBuddy IDE).
 *
 * Each selection writes a derived JSON snippet under
 * `<BUILD_DIR>/integration/mcp/exports/` containing only this round's entry
 * with machine-local absolute paths, plus the panel-import guidance the
 * installer prints. IDE global files are never read or modified, and the
 * snippet is never presented as an automatic connection.
 */

import path from 'node:path';

import { serverEntry } from '../entries.mjs';
import { BACKENDS } from '../backends.mjs';
import { AgentConfigError } from '../errors.mjs';
import { readConfigFile } from '../fileStore.mjs';

import { GUIDED_IMPORTS } from '../agents/index.mjs';
export { GUIDED_IMPORTS };

export function exportsDirFor(integrationDir) {
  return path.join(integrationDir, 'exports');
}

/** Snippet JSON text: only this round's entry, machine-local paths. */
export function ideExportSnippetText(repoRoot, backend) {
  const entry = serverEntry('mcpServers', repoRoot, backend);
  return `${JSON.stringify({ mcpServers: { [BACKENDS[backend].serverId]: entry } }, null, 2)}\n`;
}

/** Snippets are derived files; modified or foreign content must not be overwritten. */
function snippetBackend(repoRoot, file, text) {
  if (text === null) return null;
  for (const backend of Object.keys(BACKENDS)) {
    if (text === ideExportSnippetText(repoRoot, backend)) return backend;
  }
  throw new AgentConfigError(`${file}: IDE 导入片段内容已修改，请移走该文件后重新生成`, 'export-conflict');
}

export function ideExportStates(repoRoot, exportsDir) {
  const states = {};
  for (const [agentId, spec] of Object.entries(GUIDED_IMPORTS)) {
    const file = path.join(exportsDir, spec.snippetFile);
    try {
      const before = readConfigFile(repoRoot, file);
      const backend = snippetBackend(repoRoot, file, before);
      states[agentId] = { present: before !== null, configured: backend ? [BACKENDS[backend].serverId] : [] };
    } catch (error) {
      states[agentId] = { present: true, configured: [], error: error.message };
    }
  }
  return states;
}

/** Pure planning; the caller applies this step in the config batch. */
export function planIdeExport({ exportsDir, agentId, repoRoot, backend = null, readFile }) {
  const spec = GUIDED_IMPORTS[agentId];
  if (!spec) throw new Error(`guided import is not defined for ${agentId}`);
  const file = path.join(exportsDir, spec.snippetFile);
  const before = readFile(file);
  snippetBackend(repoRoot, file, before);
  const after = backend ? ideExportSnippetText(repoRoot, backend) : null;
  return {
    file, relative: path.relative(repoRoot, file).split(path.sep).join('/'), before, after,
    action: before === after ? 'unchanged' : after === null ? 'delete' : before === null ? 'create' : 'update',
    deleteFile: after === null, ideExport: agentId,
  };
}

/** Panel-import guidance lines for the installer summary. */
export function guidedImportGuidance({ agentId, snippetFile, relativeEntry, dryRun = false }) {
  const spec = GUIDED_IMPORTS[agentId];
  return [
    `${agentId} 无已核实的项目级自动写入路径，未写入任何项目配置文件；`,
    `${dryRun ? '拟生成' : '已生成'}本机导入片段 ${snippetFile}（内含 ${relativeEntry} 的绝对路径），导入步骤：`,
    ...spec.importSteps.map((step, index) => `  ${index + 1}. ${step}`),
  ];
}
