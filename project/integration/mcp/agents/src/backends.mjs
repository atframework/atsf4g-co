/**
 * Retrieval backends selectable by the installer (tgrep XOR CodeGraph).
 *
 * Kept apart from the per-agent configurator modules and the planning engine
 * so neither side imports the other just for these ids/entries.
 */

import path from 'node:path';
import { INTEGRATION_ROOT } from '../../common/src/paths.mjs';

export const SERVER_IDS = Object.freeze({
  tgrep: 'workspace-tgrep',
  codegraph: 'workspace-codegraph',
  sirchmunk: 'workspace-sirchmunk',
});

export const BACKENDS = Object.freeze({
  tgrep: {
    serverId: SERVER_IDS.tgrep,
    entry: path.join(INTEGRATION_ROOT, 'tools', 'tgrep', 'src', 'server.mjs'),
    label: 'tgrep — fast text/regex search',
  },
  codegraph: {
    serverId: SERVER_IDS.codegraph,
    entry: path.join(INTEGRATION_ROOT, 'tools', 'codegraph', 'src', 'server.mjs'),
    label: 'CodeGraph — structural code navigation',
  },
  sirchmunk: {
    serverId: SERVER_IDS.sirchmunk,
    entry: path.join(INTEGRATION_ROOT, 'tools/sirchmunk/src/server.mjs'),
    label: 'Sirchmunk — LLM document search and knowledge evolution',
  },
});

/** All managed server ids; switching backends removes both before writing one. */
export function managedServerIds() {
  return Object.values(SERVER_IDS);
}

/** Older project-prefixed ids are candidates only; their command/root must be verified. */
export function backendForServerId(id) {
  return Object.keys(BACKENDS).find(backend => id === SERVER_IDS[backend] || (typeof id === 'string' && id.endsWith(`-${backend}`))) ?? null;
}
