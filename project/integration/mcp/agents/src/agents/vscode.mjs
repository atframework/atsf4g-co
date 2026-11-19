import { JsonServerMapConfigurator } from './base.mjs';
import { SHARED_MCP_JSON_TARGET } from './mcp-json-group.mjs';

export default new JsonServerMapConfigurator({
  id: 'vscode',
  label: 'GitHub Copilot（VS Code / Visual Studio）',
  aliases: ['copilot', 'copilot-vscode', 'copilot-visual-studio', 'visual-studio', 'visualstudio'],
  target: SHARED_MCP_JSON_TARGET,
  installNotes: () => ({
    guidance: [
      'VS Code 1.140+：选择 Copilot harness，在 Chat: Open Customizations → MCP Servers 中确认服务。',
      'Visual Studio 2022 17.14+ / 2026：从解决方案目录运行 setup.js，再在 Copilot Agent 的工具列表启用并信任服务。',
    ],
    pending: ['Visual Studio 用户需在 Copilot Agent 工具列表启用所选服务。'],
  }),
});
