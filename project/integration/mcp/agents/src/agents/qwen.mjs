import { JsonServerMapConfigurator } from './base.mjs';

export default new JsonServerMapConfigurator({
  id: 'qwen',
  label: 'Qwen Code',
  format: 'mcpServersCwd',
  pathPolicy: 'cwd-relative',
  target: {
    id: 'qwen-settings-json',
    file: ['.qwen', 'settings.json'],
    evidence: 'https://qwenlm.github.io/qwen-code-docs/en/users/features/mcp/',
  },
});
