import { JsonServerMapConfigurator } from './base.mjs';

// Export file consumed through CLINE_MCP_SETTINGS_PATH by the explicit
// launcher (agents/tools/launch.mjs). Cline does not discover it natively;
// verified against the released cline@3.0.62 binary (Plan.md 11.11).
export default new JsonServerMapConfigurator({
  id: 'cline',
  label: 'Cline CLI（写入 .cline/atsf4g-mcp.json，经 launch.mjs 显式启动）',
  capability: 'export',
  installNotes: ({ dryRun }) => ({
    guidance: [
      '',
      `cline：导出文件 .cline/atsf4g-mcp.json ${dryRun ? '拟写入' : '已写入'}；启动方式：`,
      '  node project/integration/mcp/agents/tools/launch.mjs --agent=cline -- [cline 参数]',
      '启动器仅对子进程设置 CLINE_MCP_SETTINGS_PATH（显式路径替换 Cline 本次进程的默认 MCP 配置源，不合并全局服务器）；',
      '导出文件中的用户补充条目会被保留。Cline 不会自动发现该文件——这是显式接入，不等于已自动接入。',
    ],
    pending: ['cline：需经 agents/tools/launch.mjs 启动后才读取导出文件（见上方命令）'],
  }),
  target: {
    id: 'cline-export-json',
    file: ['.cline', 'atsf4g-mcp.json'],
    evidence: 'https://github.com/cline/cline/blob/main/sdk/packages/shared/src/storage/paths.ts + released cline@3.0.62 binary (CLINE_MCP_SETTINGS_PATH)',
  },
});
