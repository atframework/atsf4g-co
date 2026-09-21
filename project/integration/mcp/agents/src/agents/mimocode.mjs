import { OpenCodeShapeConfigurator } from './base.mjs';

export default new OpenCodeShapeConfigurator({
  id: 'mimocode',
  label: 'MiMo Code',
  target: {
    id: 'mimocode-json',
    file: ['.mimocode', 'mimocode.json'],
    candidates: ['mimocode.json', 'mimocode.jsonc', ['.mimocode', 'mimocode.json'], ['.mimocode', 'mimocode.jsonc']],
    evidence: 'https://mimo.xiaomi.com/mimocode/mcp-servers + https://github.com/XiaomiMiMo/MiMo-Code/blob/main/packages/opencode/src/config/config.ts',
  },
});
