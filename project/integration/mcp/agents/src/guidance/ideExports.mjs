/**
 * Native exports for explicit-load clients and IDEs without a verified
 * project-level write path. Product modules own formats and loading steps.
 *
 * Each selection writes a derived native snippet under
 * `<BUILD_DIR>/integration/mcp/exports/` containing only this round's entry
 * with machine-local absolute paths, plus the panel-import guidance the
 * installer prints. IDE global files are never read or modified, and the
 * snippet is never presented as an automatic connection.
 */

import path from 'node:path';

import { serverEntry, isOurServerEntry, entryExtraArgs } from '../entries.mjs';
import { BACKENDS, backendForServerId } from '../backends.mjs';
import { AgentConfigError } from '../errors.mjs';
import { readConfigFile } from '../fileStore.mjs';

import { GUIDED_IMPORTS } from '../agents/index.mjs';
export { GUIDED_IMPORTS };

export function exportsDirFor(integrationDir) {
  return path.join(integrationDir, 'exports');
}

/** Native snippet text: only this round's entry, machine-local paths. */
export function ideExportSnippetText(repoRoot, backend, agentId = null, launch = {}) {
  const spec = GUIDED_IMPORTS[agentId] ?? {};
  const entry = { ...serverEntry('mcpServers', repoRoot, backend, launch), ...spec.entryDefaults };
  const serverId = BACKENDS[backend].serverId;
  if (spec.render) return spec.render({ entry, serverId, repoRoot });
  return `${JSON.stringify({ [spec.format ?? 'mcpServers']: { [serverId]: entry } }, null, 2)}\n`;
}

/** Snippets are derived files; modified or foreign content must not be overwritten. */
function snippetBackend(repoRoot, file, text, agentId, launch) {
  if (text === null) return null;
  for (const backend of Object.keys(BACKENDS)) {
    if (text === ideExportSnippetText(repoRoot, backend, agentId, launch)) return backend;
  }
  // Recognize the exact serializer shape of older project-prefixed exports.
  // Extra fields, unknown arguments and any edited text still fail closed.
  const spec = GUIDED_IMPORTS[agentId] ?? {};
  try {
    let serverId, entry, regenerated;
    if (spec.render) {
      serverId = JSON.parse(text.match(/^        serverName: (.+)$/m)?.[1] ?? 'null');
      entry = { type: 'stdio', command: JSON.parse(text.match(/^        command: (.+)$/m)?.[1] ?? 'null'), args: JSON.parse(text.match(/^        args: (.+)$/m)?.[1] ?? 'null') };
      regenerated = spec.render({ entry, serverId, repoRoot });
    } else {
      const parsed = JSON.parse(text);
      const map = parsed[spec.format ?? 'mcpServers'];
      if (Object.keys(parsed).length !== 1 || !map || Object.keys(map).length !== 1) throw new Error('not a generated export');
      serverId = Object.keys(map)[0];
      entry = map[serverId];
      regenerated = `${JSON.stringify(parsed, null, 2)}\n`;
    }
    const backend = backendForServerId(serverId);
    if (backend && regenerated === text && entry.type === 'stdio' && Object.keys(entry).sort().join(',') === 'args,command,type'
      && isOurServerEntry(entry, repoRoot, launch, backend) && entryExtraArgs(entry, repoRoot, launch).length === 0) return backend;
  } catch { /* The ordinary conflict below preserves the original file. */ }
  throw new AgentConfigError(`${file}: IDE 导入片段内容已修改，请移走该文件后重新生成`, 'export-conflict');
}

export function ideExportStates(repoRoot, exportsDir, launch = {}) {
  const states = {};
  for (const [agentId, spec] of Object.entries(GUIDED_IMPORTS)) {
    const file = path.join(exportsDir, spec.snippetFile);
    try {
      const before = readConfigFile(repoRoot, file);
      const backend = snippetBackend(repoRoot, file, before, agentId, launch);
      states[agentId] = { present: before !== null, configured: backend ? [BACKENDS[backend].serverId] : [] };
    } catch (error) {
      states[agentId] = { present: true, configured: [], error: error.message };
    }
  }
  return states;
}

/** Pure planning; the caller applies this step in the config batch. */
export function planIdeExport({ exportsDir, agentId, repoRoot, backend = null, readFile, launch = {} }) {
  const spec = GUIDED_IMPORTS[agentId];
  if (!spec) throw new Error(`guided import is not defined for ${agentId}`);
  const file = path.join(exportsDir, spec.snippetFile);
  const before = readFile(file);
  snippetBackend(repoRoot, file, before, agentId, launch);
  const after = backend ? ideExportSnippetText(repoRoot, backend, agentId, launch) : null;
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
    spec.introduction ?? `${agentId} 无已核实的项目级自动写入路径，未写入任何项目配置文件；`,
    `${dryRun ? '拟生成' : '已生成'}本机导入片段 ${snippetFile}（内含 ${relativeEntry} 的绝对路径），导入步骤：`,
    ...spec.importSteps.map((step, index) => `  ${index + 1}. ${step.replaceAll('<SNIPPET_FILE>', snippetFile)}`),
  ];
}
