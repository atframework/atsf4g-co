/**
 * Compatibility re-export: agent config writing moved to the `agents/`
 * component (registry, pure planning, safe file store, batch writers) in P9.2.
 * This shim keeps the historical import path working for setup.js consumers
 * and tests; `agents/` never imports this file (see README 目录布局).
 */

export {
  AgentConfigError,
  allManagedServerIds,
  agentById,
  agentDefinitions,
  agentStates,
  BACKENDS,
  configureAgent,
  removeAgentServers,
  SERVER_IDS,
} from '../../agents/src/writers.mjs';
