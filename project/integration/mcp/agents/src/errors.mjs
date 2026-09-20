/**
 * A config file that exists but cannot be safely parsed/edited. Carrying a
 * stable `kind` lets callers distinguish damage from absence and abort before
 * writing anything (Plan.md 11.1/11.4: damaged files keep their bytes).
 */
export class AgentConfigError extends Error {
  constructor(message, kind) {
    super(message);
    this.name = 'AgentConfigError';
    this.kind = kind;
  }
}
