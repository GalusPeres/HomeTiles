import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { gunzipSync } from 'node:zlib';
import { readAdminBundle } from '../../lib/admin-bundle.mjs';
import { formatAdminDelivery } from '../../lib/admin-delivery.mjs';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const actual = readAdminBundle(repoRoot);
const read = file => fs.readFileSync(path.join(repoRoot, file), 'utf8').replace(/\r\n?/g, '\n');
assert.equal(actual.source, read('src/web/assets/admin.js'),
  'The readable assembled source must match its owning source units');
const gzipBytes = Buffer.from([...read('src/web/generated/admin_js_gzip.inc')
  .matchAll(/0x([0-9a-f]{2})/g)].map(match => Number.parseInt(match[1], 16)));
assert.equal(gunzipSync(gzipBytes).toString('utf8'), await formatAdminDelivery(actual.source),
  'The compressed firmware asset must match the verified delivery formatter');
for (const type of ['sensor', 'binary_sensor', 'energy', 'weather', 'scene',
  'navigate', 'switch', 'cover', 'media', 'climate', 'camera', 'pixelanim', 'clock', 'text']) {
  assert.ok(actual.files.some(file => file.startsWith(`src/types/${type}/admin`)),
    `${type} browser behavior must live with its type module`);
}

// Keep the isolated generator beneath the repository so Node resolves the
// pinned host dependencies without a second install or a directory junction.
const fixtureParent = path.join(repoRoot, 'build');
fs.mkdirSync(fixtureParent, {recursive: true});
const fixture = fs.mkdtempSync(path.join(fixtureParent, 'hometiles-admin-bundle-'));
const write = (file, text) => {
  const target = path.join(fixture, file);
  fs.mkdirSync(path.dirname(target), { recursive: true });
  fs.writeFileSync(target, text);
};
const writeManifest = sources => write('src/web/admin/bundle.json',
  JSON.stringify({ version: 1, sources }));
const unit = 'src/web/admin/core.js';
const outputs = ['src/web/assets/admin.js', 'src/web/generated/admin_js_gzip.inc',
  'src/web/generated/admin_css_gzip.inc', 'src/web/generated/auth_js_gzip.inc',
  'src/web/generated/admin_assets_meta.h'];
try {
  fs.mkdirSync(path.join(fixture, 'src/types'), { recursive: true });
  fs.mkdirSync(path.join(fixture, 'src/web/generated'), { recursive: true });
  write(unit, 'function example() { return 1; }\n');
  writeManifest([unit]);
  assert.equal(readAdminBundle(fixture).source, 'function example() { return 1; }\n');

  writeManifest([unit, unit]);
  assert.throws(() => readAdminBundle(fixture), /Duplicate Admin source/);
  writeManifest(['src/web/admin/../../../outside.js']);
  assert.throws(() => readAdminBundle(fixture), /Invalid Admin source path/);
  writeManifest([unit]);
  write(unit, 'function broken() {\n');
  assert.throws(() => readAdminBundle(fixture), SyntaxError);
  write(unit, 'function example() { return 1; }\n');
  write('src/types/example/admin.js', 'function other() {}\n');
  assert.throws(() => readAdminBundle(fixture), /missing from the manifest/);
  writeManifest([unit, 'src/types/example/admin.js']);
  write('src/web/assets/admin.css', 'body { color: red; }\n');
  write('src/web/assets/auth.js', 'window.HomeTilesAuth = {};\n');
  write('tools/generate-web-assets.mjs', read('tools/generate-web-assets.mjs'));
  write('tools/lib/admin-bundle.mjs', read('tools/lib/admin-bundle.mjs'));
  write('tools/lib/admin-delivery.mjs', read('tools/lib/admin-delivery.mjs'));
  const generate = (...args) => spawnSync(process.execPath,
    [path.join(fixture, 'tools/generate-web-assets.mjs'), ...args], { encoding: 'utf8' });
  const generated = generate();
  assert.equal(generated.status, 0, generated.stderr);
  const check = generate('--check');
  assert.equal(check.status, 0, check.stderr);
  const timestamp = new Date('2020-01-01T00:00:00Z');
  for (const output of outputs) fs.utimesSync(path.join(fixture, output), timestamp, timestamp);
  const repeated = generate();
  assert.equal(repeated.status, 0, repeated.stderr);
  for (const output of outputs) {
    assert.equal(fs.statSync(path.join(fixture, output)).mtimeMs, timestamp.getTime(),
      `Unchanged generation must preserve the build cache timestamp: ${output}`);
  }
  write(unit, 'function example() { return 2; }\n');
  const stale = generate('--check');
  assert.notEqual(stale.status, 0);
  assert.match(stale.stderr, /Generated file is stale: src\/web\/assets\/admin.js/);
  assert.equal(generate().status, 0);
  assert.equal(generate('--check').status, 0);
} finally {
  const resolved = path.resolve(fixture);
  assert.equal(path.dirname(resolved), path.resolve(fixtureParent));
  assert.ok(path.basename(resolved).startsWith('hometiles-admin-bundle-'));
  fs.rmSync(resolved, { recursive: true });
}
console.log('Admin bundle assembly, firmware parity and incremental generation tests passed.');
