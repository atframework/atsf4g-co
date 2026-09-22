import { OpenCodeShapeConfigurator } from './base.mjs';

// Kilo loads `./kilo.json(c)` AND `.kilo/kilo.json(c)` as separate precedence
// levels and deep-merges them — coexistence is normal. Only same-directory
// json/jsonc variants are ambiguous (exclusive → consolidated when several
// exist); machine-specific entries always go to the gitignored `.kilo/`
// location, and managed entries found in root files are migrated as legacy.
export default new OpenCodeShapeConfigurator({
  id: 'kilo',
  label: 'Kilo Code (从 .kilocode 迁移)',
  aliases: ['kilocode'],
  target: {
    id: 'kilo-json',
    file: ['.kilo', 'kilo.jsonc'],
    candidates: [['.kilo', 'kilo.jsonc'], ['.kilo', 'kilo.json']],
    legacyFiles: [
      { file: 'kilo.json', format: 'opencode' },
      { file: 'kilo.jsonc', format: 'opencode' },
      ['.kilocode', 'mcp.json'],
    ],
    evidence: 'https://kilo.ai/docs/automate/mcp/using-in-kilo-code + https://kilo.ai/docs/getting-started/settings',
  },
});
