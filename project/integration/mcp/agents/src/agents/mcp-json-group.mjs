/**
 * The `.mcp.json` physical target shared by Claude Code, pi, and CodeBuddy
 * CLI. Declared once here so the three product modules cannot drift; any
 * member selected keeps the whole group. Candidates follow
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
  evidence: 'https://code.claude.com/docs/en/mcp + https://www.codebuddy.cn/docs/cli/mcp',
});
