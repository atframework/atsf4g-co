import fs from 'node:fs';
import path from 'node:path';
import { JsonServerMapConfigurator } from './base.mjs';
import { SHARED_MCP_JSON_TARGET } from './mcp-json-group.mjs';

export default new JsonServerMapConfigurator({
  id: 'pi',
  label: 'pi (需 pi-mcp-adapter 扩展，共用 .mcp.json)',
  capability: 'extension',
  prerequisites: 'pi 本体不内置 MCP：需安装 nicobailon/pi-mcp-adapter 后才会读取项目 .mcp.json',
  installNotes: ({ repoRoot, serverIds }) => ({
    guidance: [],
    pending: [
      'pi：需安装 pi-mcp-adapter 扩展（https://github.com/nicobailon/pi-mcp-adapter）后才会读取项目 .mcp.json；扩展加载与连接待验证',
      ...piOverrideNotes(repoRoot, serverIds),
    ],
  }),
  target: SHARED_MCP_JSON_TARGET,
});

/** pi-mcp-adapter may prefer .pi/mcp.json over the shared .mcp.json (read-only check, never written). */
function piOverrideNotes(repoRoot, serverIds) {
  let text;
  try {
    text = fs.readFileSync(path.join(repoRoot, '.pi', 'mcp.json'), 'utf8');
  } catch {
    return [];
  }
  try {
    const parsed = JSON.parse(text);
    const map = parsed && typeof parsed === 'object' ? parsed.mcpServers : null;
    const hits = map && typeof map === 'object' && !Array.isArray(map) ? serverIds.filter((id) => Object.hasOwn(map, id)) : [];
    if (hits.length > 0) {
      return [`.pi/mcp.json 中已存在同名条目 ${hits.join(', ')}；pi-mcp-adapter 可能优先读取该文件而忽略 .mcp.json，请自行对齐（本安装器不修改该文件）`];
    }
    return [];
  } catch {
    return ['.pi/mcp.json 存在但无法解析；pi-mcp-adapter 可能优先读取该文件，请自行检查同名条目（本安装器不修改该文件）'];
  }
}

