// Issue #41: the Waveshare 7B with an ESP32-P4 v3.x (a v3.2 unit) gets SDIO
// at 20 MHz, no C6 firmware version query and a full system reset after a
// restart that reset only the CPUs. Only that build: the 7B before v3.0
// shares the define, every other board keeps its transport.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { getBuildProfile, releaseProfiles } from '../../device-catalog.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

const wrapper = read('src/devices/waveshare_touch_lcd_7b/waveshare_touch_lcd_7b_sdio.cpp');
assert.match(wrapper, /#if defined\(DEVICE_WAVESHARE_TOUCH_LCD_7B\) && CONFIG_ESP_REV_MIN_FULL >= 300/);
assert.match(wrapper, /__wrap_esp_hosted_get_default_sdio_config[\s\S]*config\.clock_freq_khz = 20000;/);
assert.doesNotMatch(wrapper, /bus_width/, 'the bus width stays the core default');
assert.match(wrapper, /__wrap_esp_hosted_get_coprocessor_fwversion[\s\S]*return ESP_ERR_NOT_SUPPORTED;/);
assert.match(wrapper, /RTC_NOINIT_ATTR uint32_t g_system_reset_marker/);
assert.match(wrapper, /g_system_reset_marker = kSystemResetMarker;\n  esp_rom_software_reset_system\(\);/,
  'the marker is set before the system reset');

// The board's init resets first, before any hardware is touched.
const device = read('src/devices/waveshare_touch_lcd_7b/device_waveshare_touch_lcd_7b.cpp');
assert.match(device,
  /bool DeviceWaveshareTouchLCD7B::init\(\) \{\n#if CONFIG_ESP_REV_MIN_FULL >= 300\n[^\n]*\n  waveshare_7b_reset_system_after_cpu_restart\(\);\n#endif/);

// Linker flags: only the 7B rev3 build links both wrappers.
const flags = '-Wl,--wrap=esp_hosted_get_default_sdio_config -Wl,--wrap=esp_hosted_get_coprocessor_fwversion';
assert.equal(getBuildProfile('waveshare_7b_rev3').elfFlags, flags);
assert.equal(getBuildProfile('waveshare_7b').elfFlags, '', 'the 7B before v3.0 keeps its transport');
for (const profile of releaseProfiles) {
  if (profile.key === 'waveshare_touch_lcd_7b_rev3') continue;
  assert.ok(!profile.elfFlags.includes('esp_hosted_get_coprocessor_fwversion'), `${profile.key} asks the C6 as before`);
}

// The release workflow and the local build link and check the same.
const workflow = read('.github/workflows/firmware.yml');
assert.match(workflow, /elif \[ "\$\{profile\}" = "waveshare_7b_rev3" \]; then\n[^\n]*\n\s+elf_flags="-Wl,--wrap=esp_hosted_get_default_sdio_config -Wl,--wrap=esp_hosted_get_coprocessor_fwversion"/);
assert.match(workflow, /\[ "\$\{profile\}" = "guition_jc8012p4a1" \] \|\| \[ "\$\{profile\}" = "waveshare_7b_rev3" \]/);
const localBuild = read('tools/build-firmware-local.ps1');
assert.match(localBuild, /\$Profile -eq 'waveshare_7b_rev3' -and -not \$hasJc8012SdioWrapper/);

console.log('Waveshare 7B rev3: SDIO 20 MHz, no C6 version query, system reset after a CPU restart, only this build');
