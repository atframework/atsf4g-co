/**
 * Request/result limits shared by both MCP wrappers.
 */

export const LIMITS = Object.freeze({
  /** Inbound request size (a single MCP tool-call arguments object). */
  maxRequestBytes: 256 * 1024,
  /** Search pattern length in characters. */
  maxPatternLength: 4096,
  /** Global match-row cap applied by the wrapper on top of any per-file cap. */
  maxMatchRows: 200,
  /** Context lines per side (validated range; upstream counts lines). */
  maxContextLines: 3,
  /** Cap for file listings. */
  maxFileEntries: 2000,
  /** Total serialized tool-response size in bytes. */
  maxResponseBytes: 2 * 1024 * 1024,
  /** Queries executing concurrently against one backend. */
  backendConcurrency: 1,
  /** One backend query timeout, milliseconds. */
  queryTimeoutMs: 60_000,
  /** Status queries are cheap; still bounded. */
  statusTimeoutMs: 10_000,
  /** Lifecycle stop timings: EOF grace, then SIGTERM, then SIGKILL. */
  stopGraceMs: 5_000,
  stopForceMs: 5_000,
  /** Longest single line accepted from the tgrep backend on stdout. */
  maxBackendFrameBytes: 8 * 1024 * 1024,
});

export function clampContext(value, fallback = 0) {
  if (value === null || value === undefined) {
    return fallback;
  }
  const parsed = Number(value);
  if (!Number.isFinite(parsed)) {
    return fallback;
  }
  return Math.max(0, Math.min(Math.trunc(parsed), LIMITS.maxContextLines));
}

export function clampResultCount(value, fallback) {
  if (value === null || value === undefined) {
    return fallback;
  }
  const parsed = Number(value);
  if (!Number.isFinite(parsed)) {
    return fallback;
  }
  return Math.max(1, Math.min(Math.trunc(parsed), LIMITS.maxMatchRows));
}

/**
 * Cut a record list at a whole-record boundary within count and byte caps.
 *
 * @returns {{kept: unknown[], truncated: boolean}}
 */
export function truncateRecords(records, countLimit) {
  const kept = [];
  let truncated = false;
  let size = 2; // brackets
  for (const record of records) {
    if (kept.length >= countLimit) {
      truncated = true;
      break;
    }
    const encoded = Buffer.byteLength(JSON.stringify(record) ?? 'null', 'utf8');
    if (kept.length > 0 && size + encoded + 1 > LIMITS.maxResponseBytes) {
      truncated = true;
      break;
    }
    if (kept.length === 0 && encoded + 2 > LIMITS.maxResponseBytes) {
      // A single record exceeds the whole budget: report truncation with an
      // empty list rather than emitting a partial record.
      truncated = true;
      break;
    }
    kept.push(record);
    size += encoded + (kept.length > 1 ? 1 : 0);
  }
  return { kept, truncated };
}

export function fitsRequest(payloadText) {
  return Buffer.byteLength(payloadText ?? '', 'utf8') <= LIMITS.maxRequestBytes;
}
