import { JsonServerMapConfigurator } from './base.mjs';

export default new JsonServerMapConfigurator({
  id: 'gemini',
  label: 'Gemini CLI',
  target: {
    id: 'gemini-settings-json',
    file: ['.gemini', 'settings.json'],
    evidence: 'https://geminicli.com/docs/tools/mcp-server/',
  },
});
