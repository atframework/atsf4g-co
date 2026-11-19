import { JsonServerMapConfigurator } from './base.mjs';
import { SHARED_MCP_JSON_TARGET } from './mcp-json-group.mjs';

const configurator = new JsonServerMapConfigurator({
  id: 'codebuddy-ide',
  label: 'CodeBuddy IDE（共用 .mcp.json）',
  target: SHARED_MCP_JSON_TARGET,
  installNotes: () => ({
    guidance: ['CodeBuddy IDE 4.0+：重新打开项目并在 Settings → MCP 确认工作区服务。若曾使用导入片段，在面板中清理手工导入的同名服务。'],
    pending: [],
  }),
});

// Keep recognizing old generated exports so setup can remove them transactionally.
configurator.importSpec = { snippetFile: 'codebuddy-ide-mcp-servers.json' };
export default configurator;
