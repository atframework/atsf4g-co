import { CodexTomlConfigurator } from './base.mjs';

export default new CodexTomlConfigurator({
  id: 'codex',
  label: 'Codex',
  target: {
    id: 'codex-config-toml',
    file: ['.codex', 'config.toml'],
    evidence: 'https://learn.chatgpt.com/docs/extend/mcp?surface=cli',
  },
});
