import { JsonServerMapConfigurator } from './base.mjs';

// Zed project settings are JSONC and hold unrelated settings; edits are
// surgical so foreign content (and comments) survive.
export default new JsonServerMapConfigurator({
  id: 'zed',
  label: 'Zed',
  format: 'zed',
  target: {
    id: 'zed-settings-json',
    file: ['.zed', 'settings.json'],
    evidence: 'https://zed.dev/docs/ai/mcp + https://zed.dev/docs/migrate/vs-code',
  },
});
