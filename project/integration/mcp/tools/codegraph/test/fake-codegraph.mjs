#!/usr/bin/env node
/**
 * Fake CodeGraph MCP backend for tests: a minimal stdio MCP server exposing
 * the tools named in CODEGRAPH_MCP_TOOLS (default: the full upstream set,
 * each with the pinned v1.6.0 shape including an optional projectPath).
 *
 * Behavior switches via environment:
 *   CODEGRAPH_FAKE_RECORD=<path>  append one JSON line per tools/call
 *   CODEGRAPH_FAKE_NO_STATUS=1    do not register codegraph_status
 *   CODEGRAPH_FAKE_HIDE_STATUS=1  hide status in tools/list, but keep its handler (small upstream projects)
 * Exits when stdin closes, like the upstream direct-mode server.
 */

import fs from 'node:fs';
import { loadSdk } from '../../../common/src/sdk.mjs';
const { Server } = await loadSdk('tools/codegraph', '@modelcontextprotocol/server');
const { StdioServerTransport } = await loadSdk('tools/codegraph', '@modelcontextprotocol/server/stdio');

const recordPath = process.env.CODEGRAPH_FAKE_RECORD;
const noStatus = process.env.CODEGRAPH_FAKE_NO_STATUS === '1';

const ALL = {
  codegraph_explore: {
    inputSchema: {
      type: 'object',
      properties: {
        query: { type: 'string' },
        maxFiles: { type: 'number' },
        projectPath: { type: 'string', description: 'cross-project escape hatch (must be stripped by the wrapper)' },
      },
      required: ['query'],
    },
    text: 'fake explore result',
  },
  codegraph_search: {
    inputSchema: { type: 'object', properties: { query: { type: 'string' }, kind: { type: 'string' }, limit: { type: 'number' }, projectPath: { type: 'string' } }, required: ['query'] },
    text: 'fake search result',
  },
  codegraph_callers: {
    inputSchema: { type: 'object', properties: { symbol: { type: 'string' }, file: { type: 'string' }, limit: { type: 'number' }, projectPath: { type: 'string' } }, required: ['symbol'] },
    text: 'fake callers result',
  },
  codegraph_callees: {
    inputSchema: { type: 'object', properties: { symbol: { type: 'string' }, file: { type: 'string' }, limit: { type: 'number' }, projectPath: { type: 'string' } }, required: ['symbol'] },
    text: 'fake callees result',
  },
  codegraph_impact: {
    inputSchema: { type: 'object', properties: { symbol: { type: 'string' }, file: { type: 'string' }, depth: { type: 'number' }, projectPath: { type: 'string' } }, required: ['symbol'] },
    text: 'fake impact result',
  },
  codegraph_node: {
    inputSchema: { type: 'object', properties: { symbol: { type: 'string' }, file: { type: 'string' }, includeCode: { type: 'boolean' }, projectPath: { type: 'string' } } },
    text: 'fake node result',
  },
  codegraph_status: {
    inputSchema: { type: 'object', properties: { projectPath: { type: 'string' } } },
    text: '{"files": 3, "nodes": 30, "edges": 45}',
  },
  codegraph_files: {
    inputSchema: { type: 'object', properties: { path: { type: 'string' }, pattern: { type: 'string' }, projectPath: { type: 'string' } } },
    text: 'src/a.cpp\nsrc/b.h',
  },
};

const allowlist = (process.env.CODEGRAPH_MCP_TOOLS ?? Object.keys(ALL).join(','))
  .split(',')
  .map((name) => name.trim())
  .filter((name) => name && (name !== 'codegraph_status' || !noStatus));

const server = new Server({ name: 'fake-codegraph', version: '1.6.0-fake' }, { capabilities: { tools: {} } });

server.setRequestHandler('tools/list', () => ({
  tools: allowlist.filter((name) => name !== 'codegraph_status' || process.env.CODEGRAPH_FAKE_HIDE_STATUS !== '1').map((name) => ({
    name,
    description: `fake ${name}`,
    inputSchema: ALL[name].inputSchema,
  })),
}));

server.setRequestHandler('tools/call', async (request) => {
  const name = request.params?.name;
  const args = request.params?.arguments ?? {};
  if (recordPath) {
    fs.appendFileSync(recordPath, `${JSON.stringify({ name, args })}\n`, 'utf8');
  }
  if (!allowlist.includes(name)) {
    return { content: [{ type: 'text', text: `unknown tool ${name}` }], isError: true };
  }
  if (name === 'codegraph_status' && noStatus) {
    return { content: [{ type: 'text', text: 'no status in fake' }], isError: true };
  }
  return { content: [{ type: 'text', text: ALL[name].text }], structuredContent: { tool: name, echoed_query: args.query ?? args.symbol ?? null } };
});

const transport = new StdioServerTransport();
await server.connect(transport);
process.stdin.on('end', () => process.exit(0));
process.stdin.on('close', () => process.exit(0));
process.stdin.on('error', () => process.exit(0));
