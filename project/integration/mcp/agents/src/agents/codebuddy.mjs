import { JsonServerMapConfigurator } from './base.mjs';
import { SHARED_MCP_JSON_TARGET } from './mcp-json-group.mjs';

export default new JsonServerMapConfigurator({
  id: 'codebuddy',
  label: 'CodeBuddy Code CLI (共用 .mcp.json)',
  target: SHARED_MCP_JSON_TARGET,
});
