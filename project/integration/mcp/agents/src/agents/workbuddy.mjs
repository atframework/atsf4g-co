import { JsonServerMapConfigurator } from './base.mjs';

export default new JsonServerMapConfigurator({
  id: 'workbuddy',
  label: 'WorkBuddy',
  target: {
    id: 'workbuddy-mcp-json',
    file: ['.workbuddy', 'mcp.json'],
    evidence: 'https://www.codebuddy.cn/docs/workbuddy/From-Beginner-to-Expert-Guide/Function-Description/MCP-Guide',
  },
});
