import assert from 'node:assert/strict';
import test from 'node:test';

import * as agents from '../src/agents.mjs';

test('common agents shim re-exports the agents component API', () => {
  const expected = [
    'AgentConfigError',
    'allManagedServerIds',
    'agentById',
    'agentDefinitions',
    'agentStates',
    'BACKENDS',
    'configureAgent',
    'removeAgentServers',
    'SERVER_IDS',
  ];
  for (const name of expected) {
    assert.ok(name in agents, `${name} is re-exported`);
  }
  assert.equal(agents.agentDefinitions().length, 17);
  assert.deepEqual(agents.allManagedServerIds(), ['atsf4g-tgrep', 'atsf4g-codegraph']);
  assert.ok(agents.agentById('claude'));
  assert.equal(agents.agentById('no-such-agent'), null);
});
