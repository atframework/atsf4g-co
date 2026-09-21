/**
 * Shared MCP server wiring for both wrappers: a low-level SDK Server with a
 * fixed tool table, served over stdio, with one idempotent shutdown path.
 *
 * - The outer stdout carries MCP JSON-RPC only; diagnostics go to stderr.
 * - The tool list is fixed at startup; handler failures become tool results
 *   with `isError: true` and a stable error-code payload.
 * - EOF on stdin, SIGINT, SIGTERM, and transport close all enter the same
 *   shutdown: stop the service (backend tree first), close the transport,
 *   exit. If this process is SIGKILLed, the backends observe the stdin pipe
 *   break and exit on their own (stdin-lifeline contract).
 */

import { Server } from '@modelcontextprotocol/server';
import { StdioServerTransport } from '@modelcontextprotocol/server/stdio';

import { BackendError, ErrorCodes } from './errors.mjs';

export function toolText(payload) {
  return { content: [{ type: 'text', text: JSON.stringify(payload, null, 1) }] };
}

export function toolErrorResult(error) {
  if (!(error instanceof BackendError)) {
    error = new BackendError(
      ErrorCodes.INTERNAL,
      `${error?.constructor?.name ?? 'Error'}: ${error?.message ?? String(error)}`
    );
  }
  return { ...toolText(error.toPayload()), isError: true };
}

/**
 * @param {object} options
 * @param {string} options.name server name reported in the handshake
 * @param {string} options.instructions short wrapper instructions (never the upstream's)
 * @param {Array<{name: string, description: string, inputSchema: object,
 *   annotations?: object, handler: (args: object) => Promise<object>|object}> |
 *   (() => Array<object>)} options.tools fixed tool table, or a getter when
 *   schemas refresh from the backend (tool names must stay fixed either way)
 * @param {{startup: () => void|Promise<void>, shutdown: (reason: string) => Promise<void>}} options.service
 */
export async function runWrapperServer({ name, instructions, tools, service }) {
  const server = new Server(
    { name, version: '0.1.0' },
    { capabilities: { tools: {} }, instructions }
  );
  const getTools = typeof tools === 'function' ? tools : () => tools;
  // Tool names and handlers are fixed at startup; only schema metadata may
  // refresh through the getter.
  const toolMap = new Map(getTools().map((tool) => [tool.name, tool]));

  server.setRequestHandler('tools/list', () => ({
    tools: getTools().map(({ name: toolName, description, inputSchema, annotations }) => ({
      name: toolName,
      description,
      inputSchema,
      ...(annotations ? { annotations } : {}),
    })),
  }));

  server.setRequestHandler('tools/call', async (request) => {
    const tool = toolMap.get(request.params?.name);
    if (!tool) {
      return toolErrorResult(
        new BackendError(ErrorCodes.INVALID_PARAMS, `unknown tool ${request.params?.name}`)
      );
    }
    try {
      return await tool.handler(request.params?.arguments ?? {});
    } catch (error) {
      return toolErrorResult(error);
    }
  });

  const transport = new StdioServerTransport();
  let shutdownStarted = false;
  const shutdown = async (reason) => {
    if (shutdownStarted) {
      return;
    }
    shutdownStarted = true;
    try {
      await service.shutdown(reason);
    } catch (error) {
      process.stderr.write(`[${name}] shutdown failed: ${error?.message ?? error}\n`);
    }
    try {
      await transport.close();
    } catch {
      /* transport already closed */
    }
    try {
      await server.close();
    } catch {
      /* server already closed */
    }
    process.exit(0);
  };

  process.on('SIGINT', () => void shutdown('sigint'));
  process.on('SIGTERM', () => void shutdown('sigterm'));
  // Belt-and-suspenders alongside the SDK transport's own stdin handling:
  // a closed/broken stdin means the client is gone.
  process.stdin.on('end', () => void shutdown('stdin-end'));
  process.stdin.on('close', () => void shutdown('stdin-close'));
  process.stdin.on('error', () => void shutdown('stdin-error'));
  process.on('uncaughtException', (error) => {
    process.stderr.write(`[${name}] uncaught exception: ${error?.stack ?? error}\n`);
    void shutdown('uncaught-exception');
  });

  await server.connect(transport);
  // The handshake stays fast: startup() only acquires the lock and triggers
  // the backend bring-up; long initialization proceeds in the background and
  // is reported through the status tools.
  try {
    await service.startup();
  } catch (error) {
    process.stderr.write(`[${name}] startup failed: ${error?.message ?? error}\n`);
  }
}
