#!/usr/bin/env node
/**
 * Fake patched `tgrep serve --transport stdio` for tests.
 *
 * Speaks the same newline-delimited JSON-RPC surface the wrapper touches:
 * search/files/status. Behavior switches via environment:
 *   TGREP_FAKE_NEVER_READY=1   status always reports indexing=true
 *   TGREP_FAKE_FAIL_METHOD=M   every request to method M fails (see below)
 *   TGREP_FAKE_FAIL_CODE=N     backend error code for the failure above
 *   TGREP_FAKE_DELAY_MS=N      each response is delayed by N ms
 * Exits on stdin EOF, like the patched backend.
 */

import { createInterface } from 'node:readline';

const neverReady = process.env.TGREP_FAKE_NEVER_READY === '1';
const delayMs = Number(process.env.TGREP_FAKE_DELAY_MS ?? '0');
const failMethod = process.env.TGREP_FAKE_FAIL_METHOD ?? null;
const failCode = Number(process.env.TGREP_FAKE_FAIL_CODE ?? '-32000');

const sleep = (ms) => new Promise((resolve) => {
  setTimeout(resolve, ms);
});

const stdout = process.stdout;
function write(message) {
  stdout.write(`${JSON.stringify(message)}\n`);
}

const readline = createInterface({ input: process.stdin, crlfDelay: Infinity });

readline.on('line', async (line) => {
  if (!line.trim()) {
    return;
  }
  let request;
  try {
    request = JSON.parse(line);
  } catch {
    write({ jsonrpc: '2.0', id: null, error: { code: -32700, message: 'parse error' } });
    return;
  }
  if (delayMs > 0) {
    await sleep(delayMs);
  }
  if (failMethod && request.method === failMethod) {
    write({ jsonrpc: '2.0', id: request.id, error: { code: failCode, message: `injected failure for ${failMethod}` } });
    return;
  }
  const params = request.params ?? {};
  switch (request.method) {
    case 'status':
      write({
        jsonrpc: '2.0',
        id: request.id,
        result: {
          indexing: neverReady,
          hidden_complete: !neverReady,
          files: 3,
          watch_mode: neverReady ? 'none' : 'native',
          version: 'fake-1.0.9',
        },
      });
      return;
    case 'search':
      write({
        jsonrpc: '2.0',
        id: request.id,
        result: {
          matches: [
            { file: 'src/sample.cpp', line: 10, text: `match for ${params.pattern}` },
            { file: 'src/sample.cpp', line: 42, text: `second ${params.pattern}` },
          ],
          num_matches: 2,
          elapsed_ms: 1,
          truncated: false,
          hidden_complete: true,
        },
      });
      return;
    case 'files':
      write({
        jsonrpc: '2.0',
        id: request.id,
        result: {
          files: ['src/sample.cpp', 'src/include/sample.h', 'README.md'],
        },
      });
      return;
    default:
      write({ jsonrpc: '2.0', id: request.id, error: { code: -32601, message: `unknown method ${request.method}` } });
  }
});

process.stdin.on('end', () => process.exit(0));
process.stdin.on('close', () => process.exit(0));
