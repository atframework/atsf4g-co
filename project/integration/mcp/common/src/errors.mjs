/**
 * Stable error codes surfaced through MCP tool results.
 *
 * A failure is never reported as a successful empty result: every tool error
 * carries one of these codes so agents and tests can distinguish states.
 */

export const ErrorCodes = Object.freeze({
  INDEX_NOT_READY: 'INDEX_NOT_READY',
  INDEX_IN_USE: 'INDEX_IN_USE',
  INDEX_INVALID: 'INDEX_INVALID',
  BACKEND_FAILED: 'BACKEND_FAILED',
  BACKEND_TIMEOUT: 'BACKEND_TIMEOUT',
  INVALID_PARAMS: 'INVALID_PARAMS',
  REQUEST_TOO_LARGE: 'REQUEST_TOO_LARGE',
  CANCELLED: 'CANCELLED',
  INTERNAL: 'INTERNAL',
});

export class BackendError extends Error {
  /**
   * @param {string} code one of ErrorCodes
   * @param {string} message
   * @param {unknown} [detail] JSON-serializable extra context (no source content)
   */
  constructor(code, message, detail = null) {
    super(message);
    this.name = 'BackendError';
    this.code = code;
    this.detail = detail;
  }

  toPayload() {
    const payload = { code: this.code, message: this.message };
    if (this.detail !== null && this.detail !== undefined) {
      payload.detail = this.detail;
    }
    return payload;
  }
}

export class IndexInUse extends BackendError {
  constructor(message, detail = null) {
    super(ErrorCodes.INDEX_IN_USE, message, detail);
    this.name = 'IndexInUse';
  }
}

export class IndexNotReady extends BackendError {
  constructor(message, detail = null) {
    super(ErrorCodes.INDEX_NOT_READY, message, detail);
    this.name = 'IndexNotReady';
  }
}

export class IndexInvalid extends BackendError {
  constructor(message, detail = null) {
    super(ErrorCodes.INDEX_INVALID, message, detail);
    this.name = 'IndexInvalid';
  }
}
