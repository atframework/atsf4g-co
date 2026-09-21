/**
 * Retrieval backends selectable by the installer (tgrep XOR CodeGraph).
 *
 * Kept apart from the per-agent configurator modules and the planning engine
 * so neither side imports the other just for these ids/entries.
 */

import path from 'node:path';

export const SERVER_IDS = Object.freeze({
  tgrep: 'atsf4g-tgrep',
  codegraph: 'atsf4g-codegraph',
});

export const BACKENDS = Object.freeze({
  tgrep: {
    serverId: SERVER_IDS.tgrep,
    entry: path.join('project', 'integration', 'mcp', 'tgrep', 'src', 'server.mjs'),
    label: 'tgrep — fast text/regex search',
  },
  codegraph: {
    serverId: SERVER_IDS.codegraph,
    entry: path.join('project', 'integration', 'mcp', 'codegraph', 'src', 'server.mjs'),
    label: 'CodeGraph — structural code navigation',
  },
});

/** All managed server ids; switching backends removes both before writing one. */
export function managedServerIds() {
  return Object.values(SERVER_IDS);
}
