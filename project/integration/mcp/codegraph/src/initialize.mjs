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
 * The library path is fixed by common/tools/prepare.mjs; it is never resolved from
 * the workspace's node_modules or the user's global modules.
 */

import { pathToFileURL } from 'node:url';
import { realpathSync } from 'node:fs';
import path from 'node:path';

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
});
