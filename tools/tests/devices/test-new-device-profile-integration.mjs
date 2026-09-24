import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { profiles, releaseProfiles, getBuildProfile } from '../../device-catalog.js';
import { DEVICE_PROFILES } from '../../../docs/assets/javascripts/installer-contract.mjs';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (file) => fs.readFileSync(path.join(repoRoot, file), 'utf8').replace(/\r\n/g, '\n');
const select = read('src/devices/device_select.h');
const active = read('src/devices/active_device.h');
const metadata = read('src/core/firmware/firmware_metadata.cpp');
const sketch = read('sketch.yaml');
const localBuild = read('tools/build-firmware-local.ps1');
const workflow = read('.github/workflows/firmware.yml');
const headers = new Map([...active.matchAll(/#(?:if|elif) defined\((DEVICE_\w+)\)\n#include "([^"]+)"/g)]
  .map((match) => [match[1], match[2]]));

for (const profile of profiles) {
  assert.match(select, new RegExp(`defined\\(${profile.define}\\)`), `${profile.key} must be selectable`);
  const header = headers.get(profile.define);
  assert.ok(header && fs.existsSync(path.join(repoRoot, header)), `${profile.key} active driver is missing`);
  const descriptor = metadata.match(new RegExp(`#(?:if|elif) defined\\(${profile.define}\\)\\n#define FW_META_TARGET_DEVICE_KEY "([^"]+)"`));
  assert.equal(descriptor?.[1], profile.metadataDeviceKey, `${profile.key} firmware identity`);
  const match = sketch.match(new RegExp(`^  ${profile.buildProfile}:\\n    fqbn: ([^\\n]+)$`, 'm'));
  assert.ok(match, `${profile.key} FQBN is missing`);
  const board = profile.chipFamily === 'ESP32-S3' ? 'esp32s3' : '(?:esp32p4|m5stack_tab5)';
  assert.match(match[1], new RegExp(`^esp32:esp32:${board}:`));
  assert.match(match[1], new RegExp(`FlashSize=${profile.flashSize / 1048576}M(?:,|$)`));
  if (match[1].includes('esp32p4:')) {
    assert.match(match[1], new RegExp(`ChipVariant=${profile.siliconVariant === 'pre_v3' ? 'prev3' : 'postv3'}(?:,|$)`));
  }
  assert.equal(getBuildProfile(profile.buildProfile).define, profile.define);
  if (!profile.publish) {
    assert.ok(!DEVICE_PROFILES.some((device) => device.key === profile.key));
    assert.ok(!workflow.includes(`key: ${profile.key}\n`));
    continue;
  }
  const installer = DEVICE_PROFILES.find((device) => device.key === profile.key);
  assert.ok(installer, `${profile.key} installer profile is missing`);
  assert.equal(installer.buildProfile, profile.buildProfile);
  assert.equal(installer.chipFamily, profile.chipFamily);
  assert.equal(installer.flashSize, profile.flashSize);
  assert.equal(installer.metadataDeviceKey || installer.key, profile.metadataDeviceKey);
}

// Check independently emitted consumers, rather than the spelling of their maps.
const ciKeys = [...workflow.matchAll(/^            key: (\w+)$/gm)].map((match) => match[1]);
assert.deepEqual(ciKeys.sort(), releaseProfiles.map((profile) => profile.key).sort());
assert.match(localBuild, /device-catalog\.js'\) --profile \$Profile/);
assert.match(localBuild, /\$isNativeS3 = \$buildProfile\.chipFamily -eq 'ESP32-S3'/);
assert.match(localBuild, /-D\$\(\$buildProfile\.define\)/);
assert.match(workflow, /if: matrix\.rx_variant != 'native-s3'/);
assert.match(workflow, /device-catalog\.js --verify-release-assets/);
assert.match(read('release-helper/package-ci-build.js'), /require\('\.\.\/tools\/device-catalog\.js'\)/);
assert.match(read('release-helper/import-latest-arduino-build.js'), /require\('\.\.\/tools\/device-catalog\.js'\)/);
const generated = spawnSync(process.execPath, ['tools/generate-device-profiles.mjs', '--check'], {
  cwd: repoRoot, encoding: 'utf8',
});
assert.equal(generated.status, 0, generated.stdout + generated.stderr);
console.log('All device profile integration contracts: PASS');
