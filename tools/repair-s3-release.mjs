import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {releaseProfiles, releaseAssetNames} from './device-catalog.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const marker = Buffer.from('HTTPS host=%s tls=%d (%s) allocator=%s');
const sha = data => `sha256:${crypto.createHash('sha256').update(data).digest('hex')}`;
export function replacementNames(version) {
  assert.match(version, /^v\d+\.\d+\.\d+$/);
  return releaseProfiles.filter(p => p.chipFamily === 'ESP32-S3').flatMap(p =>
    [`hometiles_${version}_${p.key}.bin`, `hometiles_${version}_${p.key}_factory.bin`]).sort();
}
export function inspectReplacements(directory, version) {
  const names = replacementNames(version);
  assert.equal(names.length, 6, 'This repair supports exactly three S3 profiles');
  assert.deepEqual(fs.readdirSync(directory).sort(), names, 'Unexpected or missing repair assets');
  return names.map(name => {
    const data = fs.readFileSync(path.join(directory, name));
    assert.ok(data.includes(marker), `${name}: OTA fix marker missing`);
    assert.ok(data.includes(Buffer.from(version)), `${name}: version missing`);
    return {name, size:data.length, digest:sha(data)};
  });
}
export function verifyAssetSet(release, version) {
  assert.deepEqual(release.assets.filter(a => a.name.endsWith('.bin')).map(a => a.name).sort(), releaseAssetNames(version));
  for (const asset of release.assets.filter(a => a.name.endsWith('.bin'))) assert.match(asset.digest ?? '', /^sha256:[0-9a-f]{64}$/);
}
export function verifyRepair(before, after, replacements, version) {
  verifyAssetSet(before, version); verifyAssetSet(after, version);
  for (const previous of before.assets.filter(a => a.name.endsWith('.bin'))) {
    const expected = replacements.find(a => a.name === previous.name) ?? previous;
    const actual = after.assets.find(a => a.name === previous.name);
    assert.equal(actual.size, expected.size, `${actual.name}: size mismatch`);
    assert.equal(actual.digest, expected.digest, `${actual.name}: digest mismatch`);
  }
}
function gh(...args) {
  const result = spawnSync('gh', args, {encoding:'utf8', maxBuffer:8*1024*1024});
  assert.equal(result.status, 0, result.stderr || result.stdout);
  return result.stdout;
}
function preflight() {
  const tag = process.env.REPAIR_RELEASE ?? '';
  replacementNames(tag);
  const version = fs.readFileSync(path.join(root, 'version.txt'), 'utf8').match(/#define FW_VERSION "([^"]+)"/)[1];
  assert.equal(tag, version, 'Repair tag must match the checked-out firmware version');
  const repo = process.env.GITHUB_REPOSITORY;
  assert.equal(repo, 'GalusPeres/HomeTiles');
  assert.match(process.env.GITHUB_SHA ?? '', /^[0-9a-f]{40}$/);
  // GitHub's by-tag endpoint excludes drafts; discover their stable release ID.
  const pages = JSON.parse(gh('api', `repos/${repo}/releases?per_page=100`, '--paginate', '--slurp'));
  const release = pages.flat().find(r => r.tag_name === tag);
  assert.ok(release, 'Existing release not found');
  assert.equal(release.draft, true, 'Only an existing draft release may be repaired');
  verifyAssetSet(release, tag);
  return {tag, repo, release};
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const {tag, repo, release:before} = preflight();
  if (process.argv[2] !== '--preflight') {
    const directory = path.resolve(process.argv[2]);
    const replacements = inspectReplacements(directory, tag);
    // All six files are verified before any existing asset is replaced.
    gh('release', 'upload', tag, ...replacements.map(a => path.join(directory, a.name)), '--repo', repo, '--clobber');
    const after = JSON.parse(gh('api', `repos/${repo}/releases/${before.id}`));
    assert.equal(after.draft, true, 'Release became public during repair');
    verifyRepair(before, after, replacements, tag);
    const proof = {tag, sourceCommit:process.env.GITHUB_SHA, runId:process.env.GITHUB_RUN_ID, replacements,
      preserved:before.assets.filter(a => a.name.endsWith('.bin') && !replacements.some(r => r.name === a.name)).map(({name,size,digest}) => ({name,size,digest}))};
    const proofFile = path.join(directory, 's3-rebuild.json');
    fs.writeFileSync(proofFile, JSON.stringify(proof, null, 2)+'\n');
    gh('release', 'upload', tag, proofFile, '--repo', repo, '--clobber');
    const notesFile = path.join(directory, 'release-notes.md');
    const note = `\n\nS3 OTA correction: internal-RAM-first TLS allocation now falls back to PSRAM on all three S3 boards. A complete GitHub OTA cycle is hardware-confirmed on Guition ESP32-4848S040; both Waveshare S3 variants still need field confirmation. S3 binaries rebuilt from [${proof.sourceCommit.slice(0,7)}](https://github.com/${repo}/commit/${proof.sourceCommit}); P4 binaries are unchanged. If device OTA fails, install the corrected regular BIN through Web Admin once.\n`;
    fs.writeFileSync(notesFile, (before.body ?? '')+note);
    gh('release', 'edit', tag, '--repo', repo, '--notes-file', notesFile, '--draft=false', '--latest');
    const published = JSON.parse(gh('api', `repos/${repo}/releases/${before.id}`));
    assert.equal(published.draft, false);
    verifyRepair(before, published, replacements, tag);
    console.log(JSON.stringify(proof, null, 2));
  }
}
