import { JsonServerMapConfigurator } from './base.mjs';
import { SHARED_MCP_JSON_TARGET } from './mcp-json-group.mjs';

export default new JsonServerMapConfigurator({
  id: 'omp',
  label: 'oh-my-pi (共用 .mcp.json)',
  target: SHARED_MCP_JSON_TARGET,
  installNotes: () => ({
    guidance: ['oh-my-pi：从工程根目录启动，并用 /mcp list 核对服务。同名的原生或兼容配置优先于根 .mcp.json。'],
    pending: [],
  }),
});
