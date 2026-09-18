/**
 * Line-delimited JSON-RPC client for the patched tgrep backend's internal
 * protocol: one JSON object per line over the supervised child's stdio.
 *
 * Only search/files/status are ever issued; `reload` is deliberately not
 * exposed. One request is in flight at a time (LIMITS.backendConcurrency);
 * every read is frame-size guarded, and a line beyond the cap fails the
 * session instead of buffering without bound.
 */

import { createInterface } from 'node:readline';

import { BackendError, ErrorCodes, IndexInvalid, IndexNotReady } from './errors.mjs';
import { LIMITS } from './limits.mjs';

function backendErrorFor(method, error) {
  const code = error?.code;
  const message = String(error?.message ?? 'backend error');
  if (code === -32001) {
    return new IndexNotReady(message);
  }
  if (code === -32002) {
    return new IndexInvalid(message);
  }
  if (code === -32602) {
    return new BackendError(ErrorCodes.INVALID_PARAMS, message, { method });
  }
  return new BackendError(ErrorCodes.BACKEND_FAILED, message, { method, backend_code: code });
}

export class LineJsonRpcClient {
  /**
   * @param {ReturnType<typeof import('./supervisor.mjs').supervise>} proc a started supervised process
   */
  constructor(proc) {
    this.proc = proc;
    this.nextId = 1;
    /** @type {Map<number, {resolve: Function, reject: Function}>} */
    this.pending = new Map();
    /** @type {Map<number, string>} request id -> method name, for error context */
    this._methodForId = new Map();
    this.closed = false;
    this.closeReason = null;
    this.chain = Promise.resolve();

    const stdout = proc.child.stdout;
    const readline = createInterface({ input: stdout, crlfDelay: Infinity });
    readline.on('line', (line) => this._onLine(line));
    proc.exited.then(() => this._failAll(new BackendError(ErrorCodes.BACKEND_FAILED, 'backend closed the response channel')));
    stdout.on('error', () => this._failAll(new BackendError(ErrorCodes.BACKEND_FAILED, 'backend stdout read failed')));
  }

  _onLine(line) {
    if (!line.trim()) {
      return;
    }
    if (Buffer.byteLength(line, 'utf8') > LIMITS.maxBackendFrameBytes) {
      this._failAll(new BackendError(ErrorCodes.BACKEND_FAILED, 'backend response frame exceeded size cap'));
      this.close();
      return;
    }
    let message;
    try {
      message = JSON.parse(line);
    } catch {
      message = { id: null, error: { code: -32700, message: 'malformed backend response' } };
    }
    if (message && typeof message === 'object' && Number.isInteger(message.id) && this.pending.has(message.id)) {
      const entry = this.pending.get(message.id);
      this.pending.delete(message.id);
      if (message.error) {
        entry.reject(backendErrorFor(this._methodForId.get(message.id), message.error));
        this._methodForId.delete(message.id);
      } else {
        entry.resolve(message.result);
        this._methodForId.delete(message.id);
      }
    }
    // Lines that match no pending id (late replies after timeout) are dropped.
  }

  /** Serialize requests: one in flight at a time. */
  request(method, params = {}, timeoutMs = LIMITS.queryTimeoutMs) {
    const run = () => this._requestNow(method, params, timeoutMs);
    const result = this.chain.then(run, run);
    this.chain = result.then(
      () => undefined,
      () => undefined
    );
    return result;
  }

  _requestNow(method, params, timeoutMs) {
    if (this.closed) {
      return Promise.reject(
        new BackendError(ErrorCodes.BACKEND_FAILED, 'backend channel is closed', { reason: this.closeReason })
      );
    }
    const id = this.nextId;
    this.nextId += 1;
    const frame = `${JSON.stringify({ jsonrpc: '2.0', id, method, params })}\n`;
    if (Buffer.byteLength(frame, 'utf8') > LIMITS.maxBackendFrameBytes) {
      return Promise.reject(new BackendError(ErrorCodes.REQUEST_TOO_LARGE, 'internal request frame exceeds the backend cap'));
    }
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        this._methodForId.delete(id);
        reject(new BackendError(ErrorCodes.BACKEND_TIMEOUT, 'backend did not answer within the query timeout', { method }));
      }, timeoutMs);
      this.pending.set(id, {
        resolve: (value) => {
          clearTimeout(timer);
          resolve(value);
        },
        reject: (error) => {
          clearTimeout(timer);
          reject(error);
        },
      });
      this._methodForId.set(id, method);
      this.proc.child.stdin?.write(frame, (error) => {
        if (error) {
          this.pending.delete(id);
          this._methodForId.delete(id);
          clearTimeout(timer);
          reject(new BackendError(ErrorCodes.BACKEND_FAILED, 'backend stdin write failed', { method }));
        }
      });
    });
  }

  _failAll(error) {
    if (this.closed) {
      return;
    }
    this.closed = true;
    this.closeReason = error.message;
    for (const entry of this.pending.values()) {
      entry.reject(error);
    }
    this.pending.clear();
  }

  close() {
    this._failAll(new BackendError(ErrorCodes.BACKEND_FAILED, 'client closed'));
  }
}
