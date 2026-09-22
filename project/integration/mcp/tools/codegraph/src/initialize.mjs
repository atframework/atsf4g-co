#!/usr/bin/env node
/**
 * One-shot CodeGraph initializer for the workspace MCP integration.
 *
 * Calls the pinned library build (never the CLI, never the installer):
 *   CodeGraph.init(root, { index: false })  -> creates .codegraph dir + DB
 *   instance.indexAll()                      -> first full index
 *   instance.close()                         -> always, in finally
 *
 * Emits one JSON object per stdout line for the wrapper:
 *   {"type":"progress","indexed":N,"total":M}
 *   {"type":"done","ok":true,"stats":{...}}
 *   {"type":"done","ok":false,"error":"..."}
 *
 * Usage: node initialize.mjs <library-dist-dir> <project-root>
 * The library path is verified by setup.js and recorded in prepared-state.json;
 * runtime startup does not repeat discovery or download missing dependencies.
 */

import { pathToFileURL } from 'node:url';
import { realpathSync } from 'node:fs';
import path from 'node:path';

// This helper belongs to the MCP session too. A closed parent pipe must not
// leave a full-repository index running after the user closes the Agent.
let activeInstance = null;
function hostClosed() {
  try { activeInstance?.close(); } catch { /* process exit releases remaining resources */ }
  process.exit(0);
}
process.stdin.once('end', hostClosed);
process.stdin.once('error', hostClosed);
process.stdin.resume();

function emit(message) {
  process.stdout.write(JSON.stringify(message) + '\n');
}

async function main() {
  const [, , libraryDirArg, rootArg] = process.argv;
  if (!libraryDirArg || !rootArg) {
    emit({ type: 'done', ok: false, error: 'usage: initialize.mjs <library-dist-dir> <project-root>' });
    process.exitCode = 2;
    return;
  }
  const libraryDir = realpathSync(libraryDirArg);
  const root = realpathSync(rootArg);
  const entry = pathToFileURL(path.join(libraryDir, 'index.js')).href;

  let CodeGraph;
  try {
    ({ CodeGraph } = await import(entry));
  } catch (error) {
    emit({ type: 'done', ok: false, error: `failed to import pinned library: ${error && error.message}` });
    process.exitCode = 2;
    return;
  }

  if (CodeGraph.isInitialized(root)) {
    // An existing index is accepted as-is here; the wrapper decides reuse
    // policy before invoking this helper. Report and let it open+sync.
    let instance = null;
    try {
      instance = await CodeGraph.open(root, { sync: false });
      emit({ type: 'done', ok: true, stats: { reused: true } });
    } catch (error) {
      emit({ type: 'done', ok: false, error: `failed to open existing index: ${error && error.message}` });
      process.exitCode = 3;
    } finally {
      try { instance && instance.close(); } catch { /* ignore */ }
    }
    return;
  }

  let instance = null;
  try {
    instance = await CodeGraph.init(root, { index: false });
    activeInstance = instance;
    const result = await instance.indexAll({
      onProgress: (progress) => {
        emit({
          type: 'progress',
          indexed: progress && progress.processed !== undefined ? progress.processed : undefined,
          total: progress && progress.total !== undefined ? progress.total : undefined,
        });
      },
    });
    emit({
      type: 'done',
      ok: true,
      stats: {
        reused: false,
        files: result && result.filesIndexed !== undefined ? result.filesIndexed : undefined,
        nodes: result && result.nodesCreated !== undefined ? result.nodesCreated : undefined,
      },
    });
  } catch (error) {
    emit({ type: 'done', ok: false, error: String(error && error.message ? error.message : error) });
    process.exitCode = 3;
  } finally {
    try { instance && instance.close(); } catch { /* ignore */ }
  }
}

main().catch((error) => {
  emit({ type: 'done', ok: false, error: `unhandled: ${error && error.message}` });
  process.exitCode = 3;
}).finally(() => {
  activeInstance = null;
  process.stdin.removeListener('end', hostClosed);
  process.stdin.removeListener('error', hostClosed);
  process.stdin.pause();
  process.stdin.unref?.();
});
