import { OpenCodeShapeConfigurator } from './base.mjs';

export default new OpenCodeShapeConfigurator({
  id: 'mimocode',
  label: 'MiMo Code',
  target: {
    id: 'mimocode-json',
    file: 'mimocode.jsonc',
    candidates: ['mimocode.jsonc', 'mimocode.json'],
    legacyFiles: [
      { file: ['.mimocode', 'mimocode.json'], format: 'opencode' },
      { file: ['.mimocode', 'mimocode.jsonc'], format: 'opencode' },
    ],
    evidence: 'https://github.com/XiaomiMiMo/MiMo-Code/blob/v0.1.17/packages/cli/src/config/config.ts',
  },
});
