import { JsonServerMapConfigurator } from './base.mjs';

export default new JsonServerMapConfigurator({
  id: 'vscode',
  label: 'VS Code / GitHub Copilot',
  format: 'vscodeServers',
  pathPolicy: 'workspace-folder',
  target: {
    id: 'vscode-mcp-json',
    file: ['.vscode', 'mcp.json'],
    evidence: 'https://code.visualstudio.com/docs/agent-customization/mcp-servers',
  },
});
