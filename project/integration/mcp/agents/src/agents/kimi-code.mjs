import { JsonServerMapConfigurator } from './base.mjs';

export default new JsonServerMapConfigurator({
  id: 'kimi-code',
  label: 'Kimi Code',
  format: 'mcpServersCwd',
  pathPolicy: 'cwd-relative',
  target: {
    id: 'kimi-code-mcp-json',
    file: ['.kimi-code', 'mcp.json'],
    evidence: 'https://github.com/MoonshotAI/kimi-code/blob/main/docs/en/customization/mcp.md',
  },
});
