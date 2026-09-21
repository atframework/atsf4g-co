import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import test from 'node:test';
import { fileURLToPath } from 'node:url';

/**
 * prepare.mjs runs `npm ci` when a component lockfile exists. npm 12 rejects
 * lockfiles whose `resolved` URLs point at tarball hosts outside the
 * configured registry (EALLOWREMOTE), so the committed lockfiles must stay
 * registry-neutral: either no `resolved` (npm fills it from the active
 * registry, mirror included) or an npmjs/npmmirror registry URL.
 */

const componentsRoot = fileURLToPath(new URL('../..', import.meta.url));
const REGISTRY_URL = /^https:\/\/registry\.(npmjs\.org|npmmirror\.com)\//;

test('component lockfiles keep registry-neutral resolved URLs', () => {
  for (const component of ['common', 'tgrep', 'codegraph']) {
    const file = path.join(componentsRoot, component, 'package-lock.json');
    if (!fs.existsSync(file)) continue;
    const lock = JSON.parse(fs.readFileSync(file, 'utf8'));
    assert.ok(lock.packages, `${component} lockfile has packages`);
    for (const [id, entry] of Object.entries(lock.packages)) {
      if (entry.resolved === undefined) continue;
      assert.match(entry.resolved, REGISTRY_URL, `${component}/${id} resolved must be registry-neutral`);
    }
  }
});
