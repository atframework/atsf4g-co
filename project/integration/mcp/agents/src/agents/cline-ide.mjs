import { GuidedImportConfigurator } from './base.mjs';

export default new GuidedImportConfigurator({
  id: 'cline-ide',
  label: 'Cline IDE 扩展（面板导入，不写项目配置）',
  importSpec: {
    snippetFile: 'cline-ide-mcp-servers.json',
    importSteps: [
      'Cline 面板 → MCP Servers（顶栏堆叠服务器图标）→ Configure 标签 → Configure MCP Servers 打开 JSON',
      '在打开文件的 mcpServers 中加入下方条目后保存，并由 Cline 面板确认服务器已连接',
    ],
  },
});
