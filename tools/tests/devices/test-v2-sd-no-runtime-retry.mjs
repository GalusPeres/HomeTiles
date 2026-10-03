// Issue #55: on a Guition JC8012P4A1 V2 whose microSD card fails to mount,
// opening Web Admin restarted the panel. Web Admin asked for the card every
// few seconds, each request retried the mount, and the failed mount's
// cleanup reset the SDMMC controller that the C6 Wi-Fi link shares (slot
// 1); the next Wi-Fi transfer crashed (xQueueGenericSend assert). A failed
// mount is now final until the next restart, and the one attempt at boot
// runs before Wi-Fi starts.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const device = read('src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.cpp');
const sketch = read('HomeTiles.ino');

const body = name => {
  const start = device.indexOf(`bool DeviceGuitionJC8012P4A1V2::${name}() {`);
  assert.ok(start >= 0, name);
  return device.slice(start, device.indexOf('\n}\n', start));
};
const init = body('initSDCard');

// A failed mount ends every later attempt before the card is touched.
const guard = init.indexOf('if (g_sd_mount_failed) {');
assert.ok(guard > 0, 'initSDCard checks the failed mount');
assert.ok(guard < init.indexOf('GuitionSDMMC.end();') && guard < init.indexOf('GuitionSDMMC.begin('),
          'the check comes before any SDMMC call');
assert.match(init.slice(guard, init.indexOf('}', guard)), /return false;/);

// Both failure paths make it final; nothing resets it at runtime.
const failures = [...init.matchAll(/g_sd_mount_failed = true;/g)].length;
assert.equal(failures, 2, 'mount failure and missing card both end the attempts');
assert.equal((device.match(/g_sd_mount_failed = false;/g) || []).length, 1, 'only the initializer clears it');
for (const name of ['suspendSDCardForNetworkTransition', 'resumeSDCardAfterNetworkTransition']) {
  assert.doesNotMatch(body(name), /g_sd_mount_failed/, name + ' keeps a failed mount final');
}
assert.match(init, /SD stays unavailable until restart/);

// The boot attempt runs before Wi-Fi (and the C6 link on slot 1) starts.
const setup = sketch.slice(sketch.indexOf('void setup() {'));
const sdBoot = setup.indexOf('BoardHAL::initSDCard();');
const network = setup.indexOf('networkManager.init();');
assert.ok(sdBoot > 0 && network > sdBoot, 'SD mounts before the network starts');
assert.doesNotMatch(setup.slice(0, sdBoot), /WiFi\.|esp_hosted/, 'nothing starts Wi-Fi before the SD attempt');

console.log('V2 SD: a failed mount is final until restart, the boot attempt runs before Wi-Fi');
