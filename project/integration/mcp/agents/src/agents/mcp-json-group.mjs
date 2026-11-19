/**
 * The project-root target shared by clients that discover `.mcp.json`.
 * Any member selected keeps the managed entry in the shared file. Candidates follow
 * CodeBuddy's official priority — `.mcp.json` first, deprecated root
 * `mcp.json` second, only the first existing file is read — so this target is
 * `priority` mode, never consolidated.
 */

export const SHARED_MCP_JSON_TARGET = Object.freeze({
  id: 'claude-mcp-json',
  file: '.mcp.json',
  candidates: ['.mcp.json', 'mcp.json'],
  candidatesMode: 'priority',
  legacyFallback: { candidate: 'mcp.json', inPlaceOwnerIds: ['codebuddy'] },
  format: 'mcpServers',
  pathPolicy: 'absolute-args',
  companionMap: { agentId: 'vscode', format: 'servers', primaryDefaults: { tools: ['*'] } },
  legacyFiles: [
    { file: ['.vscode', 'mcp.json'], format: 'servers' },
    ['.omp', 'mcp.json'],
    ['.omp', '.mcp.json'],
  ],
});
