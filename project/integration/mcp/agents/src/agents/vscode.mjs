import { JsonServerMapConfigurator } from './base.mjs';

export default new JsonServerMapConfigurator({
  id: 'vscode',
  label: 'GitHub Copilot（VS Code / Visual Studio）',
  aliases: ['copilot', 'copilot-vscode', 'copilot-visual-studio', 'visual-studio', 'visualstudio'],
  format: 'servers',
  pathPolicy: 'absolute-args',
  target: {
    id: 'vscode-mcp-json',
    file: ['.vscode', 'mcp.json'],
    evidence: 'https://code.visualstudio.com/docs/agent-customization/mcp-servers',
  },
  installNotes: () => ({
    guidance: [
      'VS Code 与 Visual Studio 共用 .vscode/mcp.json；请从解决方案目录执行 setup.js（Visual Studio 2022 17.14+ / 2026）。',
      'Visual Studio：在 Copilot Agent 的工具列表中启用服务器；此接入项不代表 Rider 的 Copilot 插件。',
    ],
    pending: [],
  }),
});
