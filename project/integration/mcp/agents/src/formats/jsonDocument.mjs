/**
 * JSON/JSONC document access for agent config files.
 *
 * Editing is surgical via the vendored Microsoft jsonc-parser
 * (vendor/jsonc-parser, UMD build loaded through createRequire — the package's
 * ESM build uses extensionless imports and cannot be loaded by Node ESM):
 * `modify` + `applyEdits` touch only the managed property, so comments,
 * foreign values, key order, and line endings survive. Formatting around an
 * inserted or deleted property can change; unrelated text is retained.
 *
 * Damage taxonomy: unreadable / invalid-json / duplicate-key /
 * not-object / map-not-object all refuse the edit and keep the file's bytes.
 */

import { createRequire } from 'node:module';

import { AgentConfigError } from '../errors.mjs';

const require = createRequire(import.meta.url);
const jsonc = require('../../vendor/jsonc-parser/lib/umd/main.js');

/** Where each format keeps its server map inside the document root. */
const ROOT_PATHS = {
  mcpServers: ['mcpServers'],
  mcpServersCwd: ['mcpServers'],
  vscodeServers: ['servers'],
  zed: ['context_servers'],
  zcode: ['mcp', 'servers'],
  opencode: ['mcp'],
};

/**
 * Find a duplicate object key in syntactically valid JSON/JSONC, or null.
 * Comment-aware: `// "x":` inside a comment is not a key. On valid input a
 * string is a key exactly when the next non-whitespace character is ':'.
 */
function findDuplicateJsonKey(text) {
  function visit(node) {
    const seen = new Set();
    for (const child of node.children ?? []) {
      if (node.type === 'object') {
        const key = child.children[0].value;
        if (seen.has(key)) return key;
        seen.add(key);
      }
      const duplicate = visit(child);
      if (duplicate !== null) return duplicate;
    }
    return null;
  }
  return visit(jsonc.parseTree(text, [], { allowTrailingComma: true }));
}

/** True when the text contains a `//` or `/*` comment (outside strings, conservatively). */
export function hasComments(text) {
  let i = 0;
  while (i < text.length) {
    const char = text[i];
    if (char === '"') {
      let end = i + 1;
      while (text[end] !== '"') {
        end += text[end] === '\\' ? 2 : 1;
      }
      i = end + 1;
      continue;
    }
    if (char === '/' && (text[i + 1] === '/' || text[i + 1] === '*')) {
      return true;
    }
    i += 1;
  }
  return false;
}

/**
 * Parse an existing JSON/JSONC document. Throws AgentConfigError on damage;
 * returns { root, text (BOM-stripped), hadBom } so callers can re-prepend the
 * BOM when they write the edited text back.
 */
export function parseJsonDocument(text, filePath) {
  const stripped = text.replace(/^\uFEFF/, '');
  const errors = [];
  const root = jsonc.parse(stripped, errors, { allowTrailingComma: true });
  if (errors.length > 0) {
    throw new AgentConfigError(`${filePath}: cannot parse as JSON/JSONC (offset ${errors[0].offset}, error ${errors[0].error})`, 'invalid-json');
  }
  const duplicate = findDuplicateJsonKey(stripped);
  if (duplicate !== null) {
    throw new AgentConfigError(`${filePath}: duplicate key ${JSON.stringify(duplicate)}`, 'duplicate-key');
  }
  if (!root || typeof root !== 'object' || Array.isArray(root)) {
    throw new AgentConfigError(`${filePath}: root is not an object`, 'not-object');
  }
  return { root, text: stripped, hadBom: stripped !== text };
}

/**
 * Walk to the server map for a format. Returns the map, or null when any
 * container along the path is absent (the edit creates it). Present-but-not-
 * object containers throw instead of being replaced.
 */
export function walkServerMap(root, format, filePath) {
  let container = root;
  for (const key of ROOT_PATHS[format]) {
    if (!Object.prototype.hasOwnProperty.call(container, key)) {
      return null;
    }
    const value = container[key];
    if (value === null || typeof value !== 'object' || Array.isArray(value)) {
      throw new AgentConfigError(`${filePath}: "${key}" is not an object`, 'map-not-object');
    }
    container = value;
  }
  return container;
}

/**
 * The single root key holding the server map, for formats that keep it at the
 * document root (candidate consolidation skips this key during the generic
 * top-level merge and handles its entries per server id instead).
 */
export function serverMapRootKey(format) {
  const segments = ROOT_PATHS[format];
  if (!segments || segments.length !== 1) {
    throw new AgentConfigError(`format ${format} does not keep its server map at the document root`, 'map-not-object');
  }
  return segments[0];
}

/** Insert one top-level property (foreign-content carry-over during consolidation). */
export function upsertJsonValue(text, segments, value) {
  return jsonc.applyEdits(text, jsonc.modify(text, segments, value, {}));
}

/** Extract comment tokens without mistaking comment-like strings for comments. */
export function commentsIn(text) {
  const comments = [];
  const stripped = text.replace(/^\uFEFF/, '');
  const scanner = jsonc.createScanner(stripped, false);
  for (let token = scanner.scan(); token !== jsonc.SyntaxKind.EOF; token = scanner.scan()) {
    if (token === jsonc.SyntaxKind.LineCommentTrivia || token === jsonc.SyntaxKind.BlockCommentTrivia) {
      comments.push(stripped.slice(scanner.getTokenOffset(), scanner.getTokenOffset() + scanner.getTokenLength()));
    }
  }
  return comments;
}

/** Set (create or replace) one server entry; returns the new document text. */
export function upsertServerEntry(text, format, serverId, entry) {
  const document = parseJsonDocument(text, '<config>');
  const existing = walkServerMap(document.root, format, '<config>')?.[serverId];
  if (existing !== undefined) {
    if (!existing || typeof existing !== 'object' || Array.isArray(existing)) {
      throw new AgentConfigError(`${serverId}: server entry is not an object`, 'entry-not-object');
    }
    for (const [key, value] of Object.entries(entry)) {
      if (JSON.stringify(existing[key]) === JSON.stringify(value)) continue;
      const edits = jsonc.modify(text, [...ROOT_PATHS[format], serverId, key], value, {});
      text = jsonc.applyEdits(text, edits);
    }
    return text;
  }
  // Do not request the optional formatter: it also reformats neighboring
  // foreign properties. New files are formatted once by the planner.
  const edits = jsonc.modify(text, [...ROOT_PATHS[format], serverId], entry, {});
  return jsonc.applyEdits(text, edits);
}

/** Remove one server entry (and nothing else); returns the new document text. */
export function removeServerEntry(text, format, serverId) {
  const tree = jsonc.parseTree(text, [], { allowTrailingComma: true });
  const value = jsonc.findNodeAtLocation(tree, [...ROOT_PATHS[format], serverId]);
  if (!value) return text;
  const property = value.parent;
  const siblings = property.parent.children;
  const index = siblings.indexOf(property);
  // Delete the property and one comma separately. A broad delete range from
  // jsonc.modify also consumes comments belonging to adjacent user entries.
  function commaBetween(start, end) {
    const scanner = jsonc.createScanner(text.slice(start, end), false);
    for (let token = scanner.scan(); token !== jsonc.SyntaxKind.EOF; token = scanner.scan()) {
      if (token === jsonc.SyntaxKind.CommaToken) return start + scanner.getTokenOffset();
    }
    return null;
  }
  const end = property.offset + property.length;
  const following = commaBetween(end, siblings[index + 1]?.offset ?? property.parent.offset + property.parent.length - 1);
  const previous = index > 0 ? siblings[index - 1] : null;
  const comma = following ?? (previous ? commaBetween(previous.offset + previous.length, property.offset) : null);
  const edits = [{ offset: property.offset, length: property.length, content: '' }];
  if (comma !== null) edits.push({ offset: comma, length: 1, content: '' });
  return jsonc.applyEdits(text, edits);
}

/**
 * True when, after managed entries were removed, the document contains
 * nothing but the (now empty) server-map containers and no comments — i.e. it
 * is a skeleton this integration could have created. Deletion additionally
 * requires the ownership record (fileStore), so a pre-existing empty object
 * the user wrote is never deleted.
 */
export function documentIsEmptySkeleton(text, format) {
  const stripped = text.replace(/^\uFEFF/, '');
  const errors = [];
  const root = jsonc.parse(stripped, errors, { allowTrailingComma: true });
  if (errors.length > 0 || !root || typeof root !== 'object' || Array.isArray(root)) {
    return false;
  }
  if (hasComments(stripped)) {
    return false;
  }
  let container = root;
  for (const key of ROOT_PATHS[format]) {
    const keys = Object.keys(container);
    if (keys.length === 0) {
      return true;
    }
    if (keys.length !== 1 || keys[0] !== key) {
      return false;
    }
    container = container[key];
  }
  return Object.keys(container).length === 0;
}
