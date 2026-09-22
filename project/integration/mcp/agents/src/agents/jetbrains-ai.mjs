import { GuidedImportConfigurator } from './base.mjs';

export default new GuidedImportConfigurator({
  id: 'jetbrains-ai',
  label: 'JetBrains AI Assistant（Rider 等 IDE 内置 AI）',
  aliases: ['rider-ai', 'rider', 'ai-assistant'],
  importSpec: {
    snippetFile: 'jetbrains-ai-mcp-servers.json',
    evidence: 'https://www.jetbrains.com/help/ai-assistant/mcp.html',
    importSteps: [
      'Settings → Tools → AI Assistant → Model Context Protocol (MCP) → Add → STDIO',
      '粘贴片段，Server level 选择当前项目，然后 Apply；路径已固定到本次工作区',
      '如需自动启用后续新增或修改的服务器，可在此设置页勾选 Automatically enable new and changed MCP servers；安装器不修改 IDE 全局设置',
      '若通过 AI Assistant 中的外部 Agent 使用，按该 Agent 的设置启用 Pass custom MCP servers',
    ],
  },
});
