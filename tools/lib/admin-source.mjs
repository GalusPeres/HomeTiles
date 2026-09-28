// Structural assertions inspect the readable source. Runtime harnesses execute
// the actual gzip delivery asset, including its host-side formatting step.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {gunzipSync} from 'node:zlib';
import {parse} from 'acorn';

export const repoRoot = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)), '..', '..');

// Windows checkouts use CRLF; structural assertions match LF source.
export function readRepoFile(...segments) {
  return fs.readFileSync(path.join(repoRoot, ...segments), 'utf8').replace(/\r\n/g, '\n');
}

export const adminSource = readRepoFile('src', 'web', 'assets', 'admin.js');

export function readAdminDeliverySource() {
  const include = readRepoFile('src', 'web', 'generated', 'admin_js_gzip.inc');
  const bytes = Buffer.from([...include.matchAll(/0x([0-9a-f]{2})/g)]
    .map(match => Number.parseInt(match[1], 16)));
  return gunzipSync(bytes).toString('utf8');
}

let deliveredFunctions;
export function extractDeliveredFunction(name) {
  if (!deliveredFunctions) {
    const source = readAdminDeliverySource();
    const ast = parse(source, {ecmaVersion: 'latest', sourceType: 'script'});
    deliveredFunctions = new Map(ast.body
      .filter(node => node.type === 'FunctionDeclaration')
      .map(node => [node.id.name, source.slice(node.start, node.end)]));
  }
  assert.ok(deliveredFunctions.has(name), `${name} must exist in the delivered Admin asset`);
  return deliveredFunctions.get(name);
}

// Returns the complete declaration of a top-level admin.js function, including
// an `async` prefix, for assertions about the readable source contract.
export function extractFunction(name, source = adminSource) {
  const marker = `function ${name}(`;
  const start = source.indexOf(marker);
  assert.notEqual(start, -1, `${name} must exist in admin.js`);
  const declarationStart =
    source.slice(Math.max(0, start - 6), start) === 'async ' ? start - 6 : start;
  const bodyStart = source.indexOf('{', start + marker.length);
  assert.notEqual(bodyStart, -1, `${name} must have a body`);
  let depth = 0;
  for (let index = bodyStart; index < source.length; index++) {
    const char = source[index];
    if (char === '{') depth++;
    if (char !== '}') continue;
    depth--;
    if (depth === 0) return source.slice(declarationStart, index + 1);
  }
  assert.fail(`${name} body is incomplete`);
}

// Inline harnesses are embedded in a <script> block, so a literal closing tag
// inside extracted production code would end that block early.
export function inlineScriptSafe(code) {
  return code.replaceAll('</script', '<\\/script');
}
