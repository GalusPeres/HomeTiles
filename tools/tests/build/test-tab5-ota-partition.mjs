// Tab5 must use the sketch's OTA layout, not the smaller board-default limit.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {getReleaseProfile} from '../../device-catalog.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const sketch = fs.readFileSync(path.join(root, 'sketch.yaml'), 'utf8');
const fqbn = sketch.match(/^  tab5:\r?\n    fqbn: ([^\r\n]+)/m)?.[1];
assert.ok(fqbn, 'Tab5 has an explicit build profile');
const options = Object.fromEntries(fqbn.split(':')[3].split(',').map(value => value.split('=')));
assert.equal(options.PartitionScheme, 'custom',
  'The Tab5 default caps the build at 6553600 bytes despite the larger sketch OTA slots');

const slots = fs.readFileSync(path.join(root, 'partitions.csv'), 'utf8')
  .split(/\r?\n/).filter(line => /^app[01],/.test(line))
  .map(line => line.split(',').map(value => value.trim()));
assert.deepEqual(slots.map(row => [row[0], Number(row[3]), Number(row[4])]),
  [['app0', 0x10000, 0x680000], ['app1', 0x690000, 0x680000]],
  'Fixing the board size check must preserve the existing OTA layout');

const build = path.join(root, 'build');
fs.mkdirSync(build, {recursive: true});
const directory = fs.mkdtempSync(path.join(build, 'test-tab5-ota-'));
const profile = getReleaseProfile('m5stacks_tab5');
function image(size, offset = 0) {
  const bytes = Buffer.alloc(size);
  bytes[offset] = 0xe9;
  bytes.writeUInt32LE(0xabcd5432, offset + 32);
  bytes.writeUInt32LE(0x44565034, offset + 288);
  bytes.write(profile.metadataDeviceKey, offset + 324);
  bytes.writeUInt32LE(0x53525634, offset + 388);
  bytes.writeUInt16LE(profile.minimumRevision, offset + 392);
  bytes.writeUInt16LE(profile.maximumRevision, offset + 394);
  bytes.write(profile.siliconVariant, offset + 396);
  return bytes;
}
function packageSize(size) {
  fs.writeFileSync(path.join(directory, 'HomeTiles.ino.bin'), image(size));
  return spawnSync(process.execPath, ['release-helper/package-ci-build.js',
    '--build-dir', directory, '--out-dir', path.join(directory, 'packaged'),
    '--device-key', profile.key, '--silicon-variant', profile.siliconVariant],
  {cwd: root, encoding: 'utf8'});
}
try {
  fs.writeFileSync(path.join(directory, 'HomeTiles.ino.merged.bin'), image(0x10000 + 512, 0x10000));
  // The size rejected by CI and the exact OTA boundary are both valid.
  for (const size of [6702778, 0x680000]) {
    const result = packageSize(size);
    assert.equal(result.status, 0, result.stdout + result.stderr);
  }
  const oversized = packageSize(0x680001);
  assert.notEqual(oversized.status, 0, 'Custom partition selection must not bypass the OTA size guard');
  assert.match(oversized.stderr, /OTA slot is only 6815744 bytes/);
} finally {
  assert.equal(path.dirname(path.resolve(directory)), path.resolve(build));
  fs.rmSync(directory, {recursive: true, force: true});
}
console.log('Tab5 custom partition profile and exact OTA size boundary: PASS');
