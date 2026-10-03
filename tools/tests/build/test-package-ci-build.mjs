import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { releaseProfiles, getReleaseProfile } from '../../device-catalog.js';
import { parseFirmwareMetadata, assertReleaseMetadata } from '../../../release-helper/firmware-metadata.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const build = path.join(root, 'build');
fs.mkdirSync(build, { recursive: true });
const directory = fs.mkdtempSync(path.join(build, 'test-package-ci-'));

function fixture(profile, offset = 0) {
  const bytes = Buffer.alloc(offset + 512);
  bytes[offset] = 0xe9;
  bytes.writeUInt32LE(0xabcd5432, offset + 32);
  const descriptor = offset + 288;
  bytes.writeUInt32LE(0x44565034, descriptor);
  bytes.write('esp32_p4_homeassistant_display', descriptor + 4);
  bytes.write(profile.metadataDeviceKey, descriptor + 36);
  const silicon = descriptor + 100;
  bytes.writeUInt32LE(0x53525634, silicon);
  bytes.writeUInt16LE(profile.minimumRevision, silicon + 4);
  bytes.writeUInt16LE(profile.maximumRevision, silicon + 6);
  bytes.write(profile.siliconVariant, silicon + 8);
  return bytes;
}

function run(profile, ota, factory) {
  const input = path.join(directory, 'input');
  fs.mkdirSync(input, { recursive: true });
  fs.writeFileSync(path.join(input, 'HomeTiles.ino.bin'), ota);
  fs.writeFileSync(path.join(input, 'HomeTiles.ino.merged.bin'), factory);
  return spawnSync(process.execPath, ['release-helper/package-ci-build.js',
    '--build-dir', input, '--out-dir', path.join(directory, profile.key),
    '--device-key', profile.key, '--metadata-device-key', profile.metadataDeviceKey,
    '--silicon-variant', profile.siliconVariant], { cwd: root, encoding: 'utf8' });
}

try {
  // Exercise actual packaging for every release identity, including both 7B variants.
  for (const profile of releaseProfiles) {
    const ota = fixture(profile);
    const factory = fixture(profile, 0x10000);
    const result = run(profile, ota, factory);
    assert.equal(result.status, 0, result.stdout + result.stderr);
    const output = path.join(directory, profile.key);
    const assets = fs.readdirSync(output).sort();
    assert.equal(assets.length, 2);
    for (const file of assets) assert.deepEqual(fs.readFileSync(path.join(output, file)), file.endsWith('_factory.bin') ? factory : ota);
  }

  const profile = getReleaseProfile('waveshare_s3_touch_lcd_4');
  const wrong = getReleaseProfile('waveshare_s3_touch_lcd_4b');
  const output = path.join(directory, profile.key);
  const before = fs.readdirSync(output).map((name) => [name, fs.readFileSync(path.join(output, name))]);
  const mismatch = run(profile, fixture(profile), fixture(wrong, 0x10000));
  assert.notEqual(mismatch.status, 0, 'Correct OTA must not allow a factory image for another board');
  assert.match(mismatch.stderr, /metadata device mismatch/);
  for (const [name, bytes] of before) assert.deepEqual(fs.readFileSync(path.join(output, name)), bytes,
    'A failed verification must preserve existing packaged firmware');

  const rev3 = getReleaseProfile('waveshare_touch_lcd_7b_rev3');
  const preV3 = getReleaseProfile('waveshare_touch_lcd_7b');
  assert.notEqual(run(rev3, fixture(rev3), fixture(preV3, 0x10000)).status, 0,
    'Factory silicon variant must match even when the metadata device key is shared');
  const v3 = getReleaseProfile('guition_jc8012p4a1_v3');
  assert.notEqual(run(v3, fixture(v3), fixture(getReleaseProfile('guition_jc8012p4a1_v2'), 0x10000)).status, 0,
    'A V2 factory image must never be packaged as the V3 image');
  const corrupt = fixture(rev3, 0x10000);
  corrupt.writeUInt16LE(400, 0x10000 + 388 + 6);
  assert.notEqual(run(rev3, fixture(rev3), corrupt).status, 0);
  assert.throws(() => parseFirmwareMetadata(Buffer.alloc(10)), /too small/);
  const badHeader = fixture(profile); badHeader[0] = 0;
  assert.throws(() => parseFirmwareMetadata(badHeader), /ESP image header/);
  const missingSilicon = fixture(profile); missingSilicon.writeUInt32LE(0, 388);
  assert.throws(() => assertReleaseMetadata(parseFirmwareMetadata(missingSilicon), profile), /silicon variant mismatch/);
} finally {
  assert.equal(path.dirname(path.resolve(directory)), path.resolve(build));
  fs.rmSync(directory, { recursive: true, force: true });
}
console.log('CI packaging metadata and factory/OTA identity: PASS');
