import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { releaseProfiles, validateCatalog, getBuildProfile, getReleaseProfile,
  installerProfiles, releaseFileVersion, releaseAssetNames, verifyReleaseAssets } from '../../device-catalog.js';
import { DEVICE_PROFILES } from '../../../docs/assets/javascripts/installer-contract.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const raw = JSON.parse(fs.readFileSync(path.join(root, 'tools/device-profiles.json'), 'utf8'));
const compatibility = JSON.parse(fs.readFileSync(
  path.join(root, 'tools/fixtures/installer-profiles-baseline.json'), 'utf8'));
const existingKeys = new Set(compatibility.profiles.map(({ key }) => key));

function assertExistingProfileCompatibility(actual) {
  // This independent fixture records the accepted installer profile contract.
  // New profiles may be inserted anywhere without changing existing values or
  // relative order, and without updating a hash or a fixed total.
  assert.deepEqual(actual.filter(({ key }) => existingKeys.has(key)), compatibility.profiles,
    'Existing installer profile values or relative order changed');
}

assertExistingProfileCompatibility(DEVICE_PROFILES);
assert.deepEqual(installerProfiles(), DEVICE_PROFILES);
const withNewProfile = structuredClone(compatibility.profiles);
withNewProfile.splice(1, 0, { key: 'additional_device' });
assertExistingProfileCompatibility(withNewProfile);
assert.throws(() => assertExistingProfileCompatibility(compatibility.profiles.slice(1)),
  /Existing installer profile/);
const reordered = structuredClone(compatibility.profiles);
[reordered[0], reordered[1]] = [reordered[1], reordered[0]];
assert.throws(() => assertExistingProfileCompatibility(reordered), /Existing installer profile/);
const changed = structuredClone(compatibility.profiles);
changed[0].flashSize /= 2;
assert.throws(() => assertExistingProfileCompatibility(changed), /Existing installer profile/);
assert.throws(() => getBuildProfile('unknown'), /unknown build profile/);
for (const buildProfile of ['layout_test_1024x600', 'layout_test_480x480']) {
  const layout = getBuildProfile(buildProfile);
  assert.equal(layout.publish, false, `${buildProfile} must remain a local-only profile`);
  assert.throws(() => getReleaseProfile(layout.key), /unknown release device/);
}

function rejects(change, pattern) {
  const invalid = structuredClone(raw);
  change(invalid);
  assert.throws(() => validateCatalog(invalid), pattern);
}
rejects((value) => { value.schemaVersion = 2; }, /schema/);
rejects((value) => value.profiles.push(value.profiles[0]), /duplicate key/);
rejects((value) => { value.profiles[1].buildProfile = value.profiles[0].buildProfile; }, /duplicate build/);
rejects((value) => { value.profiles[1].metadataDeviceKey = 'a'.repeat(32); }, /too long/);
rejects((value) => { value.profiles[0].maximumRevision = 399; }, /unsafe revision/);
rejects((value) => { value.profiles.find((entry) => entry.key.endsWith('7b_rev3_1')).maximumRevision = 302; }, /unsafe revision/);
rejects((value) => { value.profiles.find((entry) => entry.key === 'guition_jc8012p4a1_v2').rxVariant = 'repo-guition-jc8012-rx-single-block'; }, /unsafe transport/);
rejects((value) => { value.profiles.find((entry) => entry.chipFamily === 'ESP32-S3').rxVariant = 'repo-a8204'; }, /unsafe transport/);
rejects((value) => { value.profiles[1].elfFlags = '-Wl,--wrap=esp_hosted_get_default_sdio_config'; }, /unsafe linker/);
rejects((value) => { value.profiles[1].ciOrder = value.profiles[0].ciOrder; }, /CI order/);
rejects((value) => { value.profiles[0].installer.acceptsLegacyDescriptor = false; }, /legacy descriptor/);
rejects((value) => { value.profiles.find((entry) => !entry.publish).installer = value.profiles[0].installer; }, /non-release/);

const cli = spawnSync(process.execPath, ['tools/device-catalog.js', '--profile', 'waveshare_7b_rev3_1'], { cwd: root, encoding: 'utf8' });
assert.equal(cli.status, 0, cli.stderr);
assert.equal(JSON.parse(cli.stdout).metadataDeviceKey, 'waveshare_touch_lcd_7b');
assert.equal(JSON.parse(cli.stdout).key, 'waveshare_touch_lcd_7b_rev3_1');
const invalidCli = spawnSync(process.execPath, ['tools/device-catalog.js', '--profile', 'missing'], { cwd: root, encoding: 'utf8' });
assert.notEqual(invalidCli.status, 0);

const build = path.join(root, 'build');
for (const [version, filenameVersion] of [
  ['v0.6.9', 'v0.6.9'],
  ['v0.6.9-rc.1', 'v0.6.9-rc.1'],
  ['v0.6.8b6', 'v0.6.8b6'],
  ['v0.6.9+test.1', 'v0.6.9-test.1'],
]) {
  assert.equal(releaseFileVersion(version), filenameVersion);
  assert.deepEqual(releaseAssetNames(version), releaseProfiles.flatMap(({ key }) => [
    `hometiles_${filenameVersion}_${key}.bin`,
    `hometiles_${filenameVersion}_${key}_factory.bin`,
  ]).sort());
}
assert.throws(() => releaseAssetNames(''), /invalid release version/);
fs.mkdirSync(build, { recursive: true });
const directory = fs.mkdtempSync(path.join(build, 'test-device-catalog-'));
try {
  const names = releaseAssetNames('v0.6.9');
  assert.equal(names.length, releaseProfiles.length * 2);
  for (const name of names) fs.writeFileSync(path.join(directory, name), 'fixture');
  verifyReleaseAssets(directory, 'v0.6.9');
  fs.renameSync(path.join(directory, names[0]), path.join(directory, 'unexpected.bin'));
  assert.throws(() => verifyReleaseAssets(directory, 'v0.6.9'), /release asset set mismatch/);
} finally {
  assert.equal(path.dirname(path.resolve(directory)), path.resolve(build));
  fs.rmSync(directory, { recursive: true, force: true });
}
console.log('Device catalog validation and compatibility: PASS');
