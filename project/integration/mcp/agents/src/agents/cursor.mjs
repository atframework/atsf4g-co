import { JsonServerMapConfigurator } from './base.mjs';

export default new JsonServerMapConfigurator({
  id: 'cursor',
  label: 'Cursor',
  target: {
    id: 'cursor-mcp-json',
    file: ['.cursor', 'mcp.json'],
    evidence: 'https://cursor.com/docs/mcp',
  },
});
