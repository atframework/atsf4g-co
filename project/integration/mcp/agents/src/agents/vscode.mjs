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
      'Visual Studio 的 MCP 工具默认关闭；需在 Copilot Agent → 工具 → 已添加中打开所选的 workspace-tgrep / workspace-codegraph / workspace-sirchmunk，并按提示信任服务器。mcp.json 没有官方支持的默认启用字段。',
      'VS Code：如服务器已被禁用，请运行 MCP: List Servers → 选择服务器 → Enable；首次使用按提示信任服务器。自动启动不会重新启用已禁用的服务器。',
    ],
    pending: ['Visual Studio 用户：仍需在 Copilot Agent 工具列表手动启用；写入 .vscode/mcp.json 不等于工具已开启'],
  }),
});
