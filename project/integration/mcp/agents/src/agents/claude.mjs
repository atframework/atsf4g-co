import { JsonServerMapConfigurator } from './base.mjs';
import { SHARED_MCP_JSON_TARGET } from './mcp-json-group.mjs';

export default new JsonServerMapConfigurator({
  id: 'claude',
  label: 'Claude Code (与 pi、CodeBuddy CLI 共用 .mcp.json)',
  target: SHARED_MCP_JSON_TARGET,
});
