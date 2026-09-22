import { JsonServerMapConfigurator } from './base.mjs';

export default new JsonServerMapConfigurator({
  id: 'trae',
  label: 'TRAE / TraeCode（IDE 与 CLI 项目配置）',
  aliases: ['traecode', 'trae-ide', 'trae-cli'],
  target: {
    id: 'trae-mcp-json',
    file: ['.trae', 'mcp.json'],
    evidence: 'https://docs.trae.cn/ide_add-mcp-servers',
  },
  installNotes: () => ({
    guidance: ['TRAE IDE：在 Settings → MCP 中启用项目级 MCP，并确认工作区可信；CLI 读取同一 .trae/mcp.json。'],
    pending: [],
  }),
});
