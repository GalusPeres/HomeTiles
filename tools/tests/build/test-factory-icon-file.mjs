// The factory image carries the device's icon file in its LittleFS partition
// (release-helper/factory-icon-file.js): the partition from the image's own
// table, the icon size from the layout defines, the image from the ESP32
// core's mklittlefs. Without mklittlefs on this machine the image part skips.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { releaseProfiles, getReleaseProfile } from '../../device-catalog.js';

const require = createRequire(import.meta.url);
const { storagePartition, iconPxForDefine, findMklittlefs, addIconFileToFactory } =
  require('../../../release-helper/factory-icon-file.js');
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const deviceSelect = fs.readFileSync(path.join(root, 'src/devices/device_select.h'), 'utf8');

// The icon size per device follows mdi_icons.h: 1024x600 40 px, 480x480 32 px.
const px = (key) => iconPxForDefine(getReleaseProfile(key).define, deviceSelect);
assert.equal(px('guition_jc8012p4a1_v2'), 48);
assert.equal(px('m5stacks_tab5'), 48);
assert.equal(px('waveshare_touch_lcd_7b'), 40);
assert.equal(px('guition_jc1060p470c_v2'), 40);
assert.equal(px('guition_esp32_4848s040'), 32);
assert.equal(px('waveshare_touch_lcd_4_3'), 32);
for (const profile of releaseProfiles) assert.ok([32, 40, 48].includes(px(profile.key)), profile.key);

// The partition comes from the table in the image (the real one: spiffs at
// 0xD10000, 2.9 MB, before the core dump).
const kFlash = 16 * 1024 * 1024;
function table(image) {
  const entries = [['nvs', 1, 0x02, 0x9000, 0x5000], ['otadata', 1, 0x00, 0xe000, 0x2000],
    ['app0', 0, 0x10, 0x10000, 0x680000], ['app1', 0, 0x11, 0x690000, 0x680000],
    ['spiffs', 1, 0x82, 0xd10000, 0x2e0000], ['coredump', 1, 0x03, 0xff0000, 0x10000]];
  entries.forEach(([label, type, subtype, offset, size], index) => {
    const at = 0x8000 + index * 32;
    image[at] = 0xaa; image[at + 1] = 0x50; image[at + 2] = type; image[at + 3] = subtype;
    image.writeUInt32LE(offset, at + 4); image.writeUInt32LE(size, at + 8);
    image.fill(0, at + 12, at + 32);  // label padding and flags, as gen_esp32part writes them
    image.write(label, at + 12, 'latin1');
  });
}
const plain = Buffer.alloc(kFlash, 0xff);
table(plain);
assert.deepEqual(storagePartition(plain), { offset: 0xd10000, size: 0x2e0000 });
assert.throws(() => storagePartition(Buffer.alloc(0x9000, 0xff)), /no spiffs/);

const mklittlefs = findMklittlefs();
if (!mklittlefs) {
  console.log('SKIP: mklittlefs of the ESP32 core is not installed; table and icon size checks passed.');
  process.exit(0);
}

const work = fs.mkdtempSync(path.join(os.tmpdir(), 'hometiles-factory-icon-test-'));
try {
  // The icon file lands in the partition; every other byte stays.
  const icon = Buffer.from(Array.from({ length: 70001 }, (_, i) => (i * 31 + 7) & 0xff));
  const iconFile = path.join(work, 'hometiles-icons-48.bin');
  fs.writeFileSync(iconFile, icon);
  const { factory, partition, name } = addIconFileToFactory(plain, { iconFile, px: 48, mklittlefs });
  assert.equal(name, '_mdi_icons_48.bin');
  assert.equal(factory.length, plain.length, 'the factory image keeps its size');
  assert.ok(factory.subarray(0, partition.offset).equals(plain.subarray(0, partition.offset)), 'bytes before stay');
  assert.ok(factory.subarray(partition.offset + partition.size).equals(plain.subarray(partition.offset + partition.size)),
    'bytes after stay');
  const image = path.join(work, 'part.bin');
  fs.writeFileSync(image, factory.subarray(partition.offset, partition.offset + partition.size));
  // mklittlefs unpacks into a directory relative to its working directory.
  const unpacked = path.join(work, 'unpacked');
  fs.mkdirSync(unpacked);
  const unpack = spawnSync(mklittlefs, ['-u', 'unpacked', '-b', '4096', '-p', '256', '-s', String(partition.size), 'part.bin'],
    { cwd: work, encoding: 'utf8' });
  assert.equal(unpack.status, 0, unpack.stdout + unpack.stderr);
  assert.ok(fs.readFileSync(path.join(unpacked, name)).equals(icon), 'the unpacked icon file is the same file');

  assert.throws(() => addIconFileToFactory(plain.subarray(0, 0xd10000 + 100), { iconFile, px: 48, mklittlefs }),
    /ends before its LittleFS partition/);
  assert.throws(() => addIconFileToFactory(plain, { iconFile: path.join(work, 'missing.bin'), px: 48, mklittlefs }));

  // The CI packager with --icon-dir writes that factory image.
  const profile = getReleaseProfile('guition_jc8012p4a1_v2');
  const fixture = Buffer.from(plain);
  const meta = 0x10000;
  fixture.fill(0, meta, meta + 512);  // zero-padded strings, like a real image
  fixture[meta] = 0xe9;
  fixture.writeUInt32LE(0xabcd5432, meta + 32);
  fixture.writeUInt32LE(0x44565034, meta + 288);
  fixture.write('esp32_p4_homeassistant_display', meta + 292);
  fixture.write(profile.metadataDeviceKey, meta + 324);
  fixture.writeUInt32LE(0x53525634, meta + 388);
  fixture.writeUInt16LE(profile.minimumRevision, meta + 392);
  fixture.writeUInt16LE(profile.maximumRevision, meta + 394);
  fixture.write(profile.siliconVariant, meta + 396);
  const ota = Buffer.from(fixture.subarray(meta, meta + 512));
  const input = path.join(work, 'input');
  fs.mkdirSync(input);
  fs.writeFileSync(path.join(input, 'HomeTiles.ino.bin'), ota);
  fs.writeFileSync(path.join(input, 'HomeTiles.ino.merged.bin'), fixture);
  const icons = path.join(work, 'icons');
  fs.mkdirSync(icons);
  fs.writeFileSync(path.join(icons, 'hometiles-icons-48.bin'), icon);
  const out = path.join(work, 'out');
  const packaged = spawnSync(process.execPath, ['release-helper/package-ci-build.js', '--build-dir', input,
    '--out-dir', out, '--icon-dir', icons, '--device-key', profile.key,
    '--metadata-device-key', profile.metadataDeviceKey, '--silicon-variant', profile.siliconVariant],
    { cwd: root, encoding: 'utf8', env: { ...process.env, MKLITTLEFS: mklittlefs } });
  assert.equal(packaged.status, 0, packaged.stdout + packaged.stderr);
  const factoryOut = fs.readdirSync(out).find((file) => file.endsWith('_factory.bin'));
  const written = fs.readFileSync(path.join(out, factoryOut));
  assert.ok(written.subarray(0, 0xd10000).equals(fixture.subarray(0, 0xd10000)), 'firmware part unchanged');
  assert.ok(!written.subarray(0xd10000, 0xd10000 + 0x2e0000).equals(fixture.subarray(0xd10000, 0xd10000 + 0x2e0000)),
    'LittleFS part filled');

  // A missing icon file fails the packaging before old outputs are removed.
  fs.rmSync(path.join(icons, 'hometiles-icons-48.bin'));
  const failed = spawnSync(process.execPath, ['release-helper/package-ci-build.js', '--build-dir', input,
    '--out-dir', out, '--icon-dir', icons, '--device-key', profile.key,
    '--metadata-device-key', profile.metadataDeviceKey, '--silicon-variant', profile.siliconVariant],
    { cwd: root, encoding: 'utf8', env: { ...process.env, MKLITTLEFS: mklittlefs } });
  assert.notEqual(failed.status, 0);
  assert.ok(fs.readFileSync(path.join(out, factoryOut)).equals(written), 'existing output kept');
} finally {
  fs.rmSync(work, { recursive: true, force: true });
}
console.log('PASS: the factory image carries the device icon file in its LittleFS partition');
