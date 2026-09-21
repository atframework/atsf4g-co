import { GuidedImportConfigurator } from './base.mjs';

export default new GuidedImportConfigurator({
  id: 'codebuddy-ide',
  label: 'CodeBuddy IDE（面板导入，不写项目配置）',
  importSpec: {
    snippetFile: 'codebuddy-ide-mcp-servers.json',
    importSteps: [
      'CodeBuddy IDE → Settings → MCP → Add MCP 打开配置文件',
      '在文件的 mcpServers 中加入下方条目后保存，并在设置页确认服务器已启用',
    ],
  },
});
