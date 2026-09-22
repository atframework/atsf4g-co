import { GuidedImportConfigurator } from './base.mjs';

export default new GuidedImportConfigurator({
  id: 'copilot-jetbrains',
  label: 'GitHub Copilot（Rider / JetBrains IDEs）',
  aliases: ['copilot-rider', 'rider-copilot'],
  importSpec: {
    snippetFile: 'copilot-jetbrains-mcp-servers.json',
    format: 'servers',
    evidence: 'https://docs.github.com/en/copilot/how-tos/provide-context/use-mcp-in-your-ide/extend-copilot-chat-with-mcp?tool=jetbrains',
    importSteps: [
      'Rider 或其他 JetBrains IDE → Copilot Chat → Agent → Configure your MCP server → Add MCP Tools',
      '在插件打开的 mcp.json 的 servers 中合并片段；支持 Workspace 作用域的版本优先选当前工作区',
      '保存并在 Copilot 工具列表确认服务器；本安装器不猜测插件版本对应的内部配置路径',
    ],
  },
});
