import { OpenCodeShapeConfigurator } from './base.mjs';

export default new OpenCodeShapeConfigurator({
  id: 'opencode',
  label: 'OpenCode',
  target: {
    id: 'opencode-json',
    file: 'opencode.json',
    candidates: ['opencode.json', 'opencode.jsonc'],
    evidence: 'https://opencode.ai/docs/mcp-servers/ + https://opencode.ai/docs/config/',
  },
});
