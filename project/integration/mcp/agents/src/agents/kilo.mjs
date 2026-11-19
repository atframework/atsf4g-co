import { OpenCodeShapeConfigurator } from './base.mjs';

// Kilo merges both project layers, with .kilo/ taking precedence.
// Move only our entries to the root and preserve other directory settings.
export default new OpenCodeShapeConfigurator({
  id: 'kilo',
  label: 'Kilo Code (从 .kilocode 迁移)',
  aliases: ['kilocode'],
  target: {
    id: 'kilo-json',
    file: 'kilo.jsonc',
    candidates: ['kilo.jsonc', 'kilo.json'],
    legacyFiles: [
      { file: ['.kilo', 'kilo.json'], format: 'opencode' },
      { file: ['.kilo', 'kilo.jsonc'], format: 'opencode' },
      ['.kilocode', 'mcp.json'],
    ],
    evidence: 'https://kilo.ai/docs/automate/mcp/using-in-kilo-code + https://kilo.ai/docs/getting-started/settings',
  },
});
