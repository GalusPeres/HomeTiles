import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const vendor = path.join(root, 'src/devices/guition_jc8012p4a1_v2/vendor');
const sourcePath = path.join(vendor, 'i2c_master_internal.c');
const source = fs.readFileSync(sourcePath, 'utf8').replaceAll('\r\n', '\n');
const allocation = source.match(/i2c_master = heap_caps_calloc\([^;]+;/g);
assert.equal(allocation?.length, 1);
assert.match(allocation[0], /MALLOC_CAP_INTERNAL \| MALLOC_CAP_8BIT\);$/,
  'The bus object contains atomics and must never be allocated in PSRAM');
assert.match(source, /#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL\(5, 5, 2\)/);

// Verify that the entire driver remains the pinned upstream implementation
// apart from the allocation fix and the profile/version envelope.
const original = source.slice(source.indexOf('/*\n * SPDX-FileCopyrightText:'),
  source.lastIndexOf('\n#endif  // DEVICE_GUITION_JC8012P4A1_V2\n'))
  .replace(allocation[0], allocation[0].replace('MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT', 'I2C_MEM_ALLOC_CAPS'));
const digest = text => crypto.createHash('sha256').update(text).digest('hex');
assert.equal(digest(original), '1b45fd279271a39464e136693fb991ea09e711409ecf0ceb59dfb307aa0e5e73');
assert.equal(digest(fs.readFileSync(path.join(vendor, 'i2c_private.h'), 'utf8').replaceAll('\r\n', '\n')),
  '89534a28f2566308c04f8d97e21faa95bdd8f2e04a2c65023a154d73f27eef26');

const cc = ['clang', 'gcc'].find(c => spawnSync(c, ['--version']).status === 0);
if (!cc) {
  console.log('SKIP: profile isolation preprocessing needs clang or gcc');
} else {
  for (const profile of ['DEVICE_WAVESHARE_TOUCH_LCD_8', 'DEVICE_GUITION_JC8012P4A1', 'DEVICE_M5STACKS_TAB5']) {
    const result = spawnSync(cc, ['-E', '-P', '-x', 'c', '-DHOMETILES_CI_TARGET', `-D${profile}`, '-I', root, sourcePath], {encoding: 'utf8'});
    assert.equal(result.status, 0, result.stderr);
    assert.equal(result.stdout.trim(), '', `${profile} must retain the SDK driver`);
  }
}
console.log('PASS: Guition V2 I2C internal allocation, pinned backport and profile isolation');
