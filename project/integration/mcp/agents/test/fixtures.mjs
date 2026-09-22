/** Older planner tests model a toolkit installed at tools/mcp in each fixture. */
import path from 'node:path';
import * as writers from '../src/writers.mjs';
import { planConfigChanges as plan } from '../src/configPlan.mjs';
export * from '../src/writers.mjs';
const context = options => ({ ...options, launch: options.launch ?? { integrationRoot: path.join(options.repoRoot, 'tools/mcp') } });
export const configureAgent = options => writers.configureAgent(context(options));
export const removeAgentServers = options => writers.removeAgentServers(context(options));
export const planAgentConfigChanges = options => writers.planAgentConfigChanges(context(options));
export const runAgentConfigBatch = options => writers.runAgentConfigBatch(context(options));
export const planConfigChanges = options => plan(context(options));
export const agentStates = (root, launch) => writers.agentStates(root, launch ?? { integrationRoot: path.join(root, 'tools/mcp') });
