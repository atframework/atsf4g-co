import { JsonServerMapConfigurator } from './base.mjs';

export default new JsonServerMapConfigurator({
  id: 'zcode',
  label: 'ZCode',
  format: 'zcode',
  target: {
    id: 'zcode-config-json',
    file: ['.zcode', 'config.json'],
    evidence: 'https://www.zcode.network/en/docs/mcp-services/',
  },
});
