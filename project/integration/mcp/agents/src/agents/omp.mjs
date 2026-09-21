import { JsonServerMapConfigurator } from './base.mjs';

export default new JsonServerMapConfigurator({
  id: 'omp',
  label: 'oh-my-pi (omp)',
  target: {
    id: 'omp-mcp-json',
    file: ['.omp', 'mcp.json'],
    evidence: 'https://github.com/can1357/oh-my-pi/blob/main/docs/mcp-config.md',
  },
});
