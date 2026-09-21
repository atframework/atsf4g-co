/** Lossless value merge: compatible object fields combine, conflicting leaves fail closed. */
import { AgentConfigError } from '../errors.mjs';
import { commentsIn, parseJsonDocument, upsertJsonValue, walkServerMap } from '../formats/jsonDocument.mjs';

function isObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

export function equalValues(left, right) {
  if (left === right) return true;
  if (Array.isArray(left) && Array.isArray(right)) {
    return left.length === right.length && left.every((value, index) => equalValues(value, right[index]));
  }
  if (!isObject(left) || !isObject(right)) return false;
  const keys = Object.keys(left);
  return keys.length === Object.keys(right).length && keys.every((key) => Object.hasOwn(right, key) && equalValues(left[key], right[key]));
}

export function mergeJsonValues(text, incoming, { destination, source, segments = [] }) {
  // Re-read the document after insertions; never assign user keys to a normal
  // object (e.g. __proto__) and never serialize unrelated existing values.
  function merge(value, location) {
    let current = parseJsonDocument(text, destination).root;
    for (const key of location) {
      if (!Object.hasOwn(current, key)) {
        text = upsertJsonValue(text, location, value);
        return;
      }
      current = current[key];
    }
    if (equalValues(current, value)) return;
    if (isObject(current) && isObject(value)) {
      for (const [key, child] of Object.entries(value)) merge(child, [...location, key]);
      return;
    }
    throw new AgentConfigError(
      `${source} 与 ${destination} 中 ${JSON.stringify(location)} 的值不一致；请先手工统一（本轮未写入任何文件）`,
      'consolidation-conflict',
    );
  }
  merge(incoming, segments);
  return text;
}

export function mergeCandidateDocuments({ target, destination, sources, contents }) {
  const original = parseJsonDocument(contents.get(destination), destination);
  walkServerMap(original.root, target.format, destination);
  let text = original.text;
  for (const source of sources) {
    const document = parseJsonDocument(contents.get(source), source);
    walkServerMap(document.root, target.format, source);
    text = mergeJsonValues(text, document.root, { destination, source });
    text = appendMigratedComments(text, source, commentsIn(document.text));
  }
  return `${original.hadBom ? '\uFEFF' : ''}${text}`;
}

export function appendMigratedComments(text, source, comments) {
  if (comments.length === 0) return text;
  const eol = text.includes('\r\n') ? '\r\n' : '\n';
  // Locations disappear with the source properties/file; retain every token
  // in a labeled block, preserving text apart from the destination's EOL.
  return text + `${eol}// Migrated comments from ${JSON.stringify(source)}${eol}`
    + comments.map((comment) => comment.replace(/\r?\n/g, eol)).join(eol) + eol;
}

export function removedComments(before, after) {
  const remaining = commentsIn(after);
  return commentsIn(before).filter((comment) => {
    const index = remaining.indexOf(comment);
    if (index === -1) return true;
    remaining.splice(index, 1);
    return false;
  });
}
