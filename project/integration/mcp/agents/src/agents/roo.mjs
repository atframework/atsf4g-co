import { JsonServerMapConfigurator } from './base.mjs';

export default new JsonServerMapConfigurator({
  id: 'roo',
  label: 'Roo Code (从 .roo/mcp_settings.json 迁移)',
  target: {
    id: 'roo-mcp-json',
    file: ['.roo', 'mcp.json'],
    entryDefaults: { disabled: false },
    legacyFiles: [['.roo', 'mcp_settings.json']],
    evidence: 'https://roocodeinc.github.io/Roo-Code/features/mcp/using-mcp-in-roo/',
  },
});
