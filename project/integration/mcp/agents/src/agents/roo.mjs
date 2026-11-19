import { JsonServerMapConfigurator } from './base.mjs';

export default new JsonServerMapConfigurator({
  id: 'roo',
  label: 'Zoo Code / Roo Code',
  aliases: ['zoo', 'zoocode'],
  target: {
    id: 'roo-mcp-json',
    file: ['.roo', 'mcp.json'],
    entryDefaults: { disabled: false },
    legacyFiles: [['.roo', 'mcp_settings.json']],
    evidence: 'https://docs.zoocode.dev/features/mcp/using-mcp-in-roo',
  },
});
