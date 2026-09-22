import { GuidedImportConfigurator } from './base.mjs';

export default new GuidedImportConfigurator({
  id: 'dsh',
  label: 'DSH / DeepSeek Harness（原生 Cordis patch）',
  aliases: ['deepseek-harness'],
  capability: 'export',
  importSpec: {
    snippetFile: 'dsh-mcp.cordis.yml',
    evidence: 'https://github.com/deepseek-ai/deepseek-harness/blob/master/docs/user/guide/mcp-memory.md',
    introduction: 'DSH 使用官方 MCP 插件与显式 --patch 接入；导出文件不自动加载。',
    render({ entry, serverId }) {
      // JSON strings and arrays are YAML flow scalars/sequences. Never emit !!js
      // or interpolate shell code into a user configuration.
      return [
        '- insert:',
        `    - id: ${JSON.stringify(`mcp-${serverId}`)}`,
        "      name: '@deepseek-ai/dsh-mcp-client'",
        '      config:',
        `        serverName: ${JSON.stringify(serverId)}`,
        '        transport: stdio',
        `        command: ${JSON.stringify(entry.command)}`,
        `        args: ${JSON.stringify(entry.args)}`,
      ].join('\n') + '\n';
    },
    importSteps: [
      '确认当前 DSH profile 能解析 @deepseek-ai/dsh-mcp-client（版本与 DSH 宿主匹配）',
      '在当前工作区执行 dsh web --patch "<SNIPPET_FILE>"；其他 profile 使用其对应启动命令并传同一 --patch',
      '在 DSH 工具列表确认 mcp__<serverName>__<tool>；省略 --patch 即不加载本片段，不覆盖已有 Cordis 配置',
    ],
  },
});
