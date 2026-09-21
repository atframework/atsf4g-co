/**
 * Compatibility surface for the product registry.
 *
 * Products and physical targets now live in per-agent configurator modules
 * under `agents/src/agents/` (one file per product, shared config styles as
 * base classes — Plan.md 11.17); backends live in `backends.mjs`. This module
 * only re-exports both surfaces under the import paths the engine, setup.js,
 * and the tests have always used:
 *
 * - `BACKENDS/SERVER_IDS/managedServerIds` — selectable retrieval backends.
 * - `TARGETS` — physical config files (id → descriptor).
 * - `REGISTRY/agentDefinitions/agentById` — selectable products.
 * - `targetFor/legacyLocations/autoConfigurableAgents/guidedAgents/productsForTarget`
 *   — target↔product helpers.
 */

export { BACKENDS, SERVER_IDS, managedServerIds } from './backends.mjs';
export {
  CONFIGURATORS,
  REGISTRY,
  TARGETS,
  agentById,
  agentDefinitions,
  autoConfigurableAgents,
  guidedAgents,
  legacyLocations,
  productsForTarget,
  targetFor,
} from './agents/index.mjs';
