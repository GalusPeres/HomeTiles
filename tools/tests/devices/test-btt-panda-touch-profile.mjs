// BIGTREETECH Panda Touch (5 inch 800 x 480 ESP32-S3 printer panel, PR #144).
// This contract checks the device profile (catalog, build profile, firmware
// identity, installer entry, setup access point), the grid and popup card of
// the other 800 x 480 panel (no device extras), the panel wiring and timing
// measured on the hardware and listed in BIGTREETECH's PINOUT.md, the
// backlight at BIGTREETECH's 30 kHz, the I/O pins of the expansion header, the GT900 register map (status 0x814E, point block 0x8150 with X
// first), the framebuffer handling (two PSRAM buffers with the panel reading
// one while the UI draws into the other, a bounce buffer in DRAM instead of
// a DMA that reads PSRAM, and a row by row cache write back instead of one
// range over the whole area), and that the other S3 drivers keep their own
// values.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {getBuildProfile, releaseProfiles} from '../../device-catalog.js';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

const PANDA = 'DEVICE_BTT_PANDA_TOUCH';
const pandaKey = 'btt_panda_touch';
const driver = read('src/devices/btt_panda_touch/device_btt_panda_touch.cpp');
const header = read('src/devices/btt_panda_touch/device_btt_panda_touch.h');

// --- Catalog, build profile and firmware identity ----------------------------
const panda = getBuildProfile(pandaKey);
assert.equal(panda.key, pandaKey);
assert.equal(panda.metadataDeviceKey, pandaKey);
assert.equal(panda.define, PANDA);
assert.equal(panda.chipFamily, 'ESP32-S3');
assert.equal(panda.flashSize, 16 * 1024 * 1024);
assert.equal(panda.siliconVariant, 'default');
assert.equal(panda.rxVariant, 'native-s3');
assert.equal(panda.elfFlags, '');
assert.equal(panda.publish, true);
assert.equal(panda.installer.status, 'validation-pending');
assert.match(panda.installer.label, /\(5 inch\)/, 'BIGTREETECH lists a 5 inch display');
assert.ok(releaseProfiles.some(profile => profile.key === pandaKey));
// After the open device branches (T3E v2.4 23, CrowPanel 24).
assert.equal(panda.ciOrder, 25);
assert.ok(read('src/web/setup/web_config.cpp').includes(
  `#elif defined(${PANDA})\n  return "BTT_Panda_Touch_Config";`), 'Its own setup access point');

const select = read('src/devices/device_select.h');
assert.match(select, new RegExp(`// #define ${PANDA}`), 'The profile is selectable');
assert.match(read('src/devices/active_device.h'), new RegExp(`${PANDA}\\)\\n#include "src/devices/btt_panda_touch/device_btt_panda_touch.h"`));
assert.match(read('src/core/firmware/firmware_metadata.cpp'),
  new RegExp(`#elif defined\\(${PANDA}\\)\\n#define FW_META_TARGET_DEVICE_KEY "${pandaKey}"\\n#define FW_META_TARGET_DISPLAY_NAME "BTT Panda Touch"`));

const sketch = read('sketch.yaml');
const sketchProfile = name =>
  sketch.match(new RegExp(`\\n  ${name}:\\n([\\s\\S]*?)(?=\\n  [A-Za-z0-9_]+:\\n|$)`))?.[1];
assert.ok(sketchProfile(pandaKey), 'The sketch has a build profile');
assert.match(sketchProfile(pandaKey), /esp32:esp32:esp32s3:/);
assert.match(sketchProfile(pandaKey), /FlashSize=16M/);
assert.match(sketchProfile(pandaKey), /PSRAM=opi/);
assert.match(sketchProfile(pandaKey), /CDCOnBoot=default/,
  'The USB-C port is a CH340K bridge, so the USB CDC is off');

// --- Panel: pins, timing, no init sequence ----------------------------------
assert.match(header, /"btt_panda_touch",/);
assert.match(header, /"BTT Panda Touch",/);
// No device extras (the closest panel's contract): the grid of the
// Waveshare Touch LCD 4.3 and the shared square popup card.
const waveshare43 = read('src/devices/waveshare_touch_lcd_4_3/profile.h');
const grid = text => text.match(/800,\s*480,\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),/)?.slice(1);
assert.deepEqual(grid(header), grid(waveshare43), 'The grid of the other 800 x 480 panel');
assert.deepEqual(grid(header), ['5', '4', '10', '3', '150', '111']);
assert.doesNotMatch(read('src/ui/popups/popup_layout.h'), /PANDA/, 'The shared popup card, no card of its own');
assert.match(driver, /constexpr int16_t kScreenWidth = 800;/);
assert.match(driver, /constexpr int16_t kScreenHeight = 480;/);
assert.match(driver, /constexpr int8_t kPanelPclk = 5;/);
assert.match(driver, /constexpr int8_t kPanelDe = 38;/);
assert.match(driver, /constexpr int8_t kPanelReset = 46;/);
assert.match(driver, /constexpr int8_t kBacklightPin = 21;/);
assert.match(driver, /constexpr uint32_t kPanelPclkHz = 14 \* 1000 \* 1000;/,
  'The pixel clock stays at the rate the panel holds under load');
assert.match(driver, /constexpr uint32_t kBacklightFrequency = 30000;/,
  'The backlight at the rate of BIGTREETECH: above hearing, no flicker');
assert.match(driver, /config\.timings\.flags\.pclk_active_neg = true;/);
assert.match(driver, /config\.hsync_gpio_num = GPIO_NUM_NC;/);
assert.match(driver, /config\.vsync_gpio_num = GPIO_NUM_NC;/);
assert.match(driver, /kPanelData\[16\] = \{17, 18, 48, 47, 39, 11, 12, 13,\s*14, 15, 16, 6,  7,  8,  9,  10\};/,
  'The RGB bus carries B3..B7, G2..G7 and R3..R7');

// --- Framebuffer: two PSRAM buffers plus a DRAM bounce buffer ---------------
assert.match(driver, /config\.num_fbs = 2;/,
  'The UI is drawn into the buffer the panel does not scan');
assert.match(driver, /config\.bounce_buffer_size_px = kScreenWidth \* 30;/,
  'The bounce buffer in DRAM gives the panel interrupt room under load');
assert.match(driver, /config\.flags\.bb_invalidate_cache = true;/);
assert.match(driver, /config\.flags\.double_fb = true;/);
assert.match(driver, /err = esp_lcd_rgb_panel_get_frame_buffer\(g_panel, 2, &fb0, &fb1\);/,
  'Both framebuffers come from the driver, never out of bounds');
assert.match(driver, /Only switch the panel on a frame that drew something|g_damageX1 < g_damageX0/,
  'A frame that drew nothing must not switch the panel');
assert.match(driver, /for \(int32_t row = y; row < y \+ h; \+\+row\)/,
  'The cache write back walks the rows, the framebuffer rows are not contiguous');
assert.match(driver, /ESP_CACHE_MSYNC_FLAG_UNALIGNED/,
  'The cache write back uses the unaligned flag, like the panel driver itself');
assert.match(driver, /if \(x <= 0 && w >= kScreenWidth\)/,
  'A band that covers the full width is one continuous block');

// --- Touch: the GT900 register map ------------------------------------------
assert.match(driver, /kTouchAddressPrimary = 0x5D;/);
assert.match(driver, /kTouchSda = 2;/);
assert.match(driver, /kTouchScl = 1;/);
assert.match(driver, /kTouchInt = 40;/);
assert.match(driver, /kTouchReset = 41;/);
assert.match(driver, /kTouchStatusRegister = 0x814E;/);
assert.match(driver, /kTouchPointRegister = 0x8150;/);
assert.match(driver, /const uint16_t raw_x = static_cast<uint16_t>\(point\[0\] \| \(point\[1\] << 8\)\);/,
  'The GT900 point starts with X, the track id comes later');
assert.match(driver, /const uint16_t raw_y = static_cast<uint16_t>\(point\[2\] \| \(point\[3\] << 8\)\);/);
assert.match(driver, /writeTouchRegister\(kTouchStatusRegister, 0\)/,
  'A frame is acknowledged with a zero write to the status register');
assert.match(driver, /raw_x >= kScreenWidth \|\| raw_y >= kScreenHeight/,
  'The controller already reports screen coordinates');

// --- Storage and hardware I/O -----------------------------------------------
assert.match(driver, /LittleFS\.begin\(true, "\/littlefs", 10, "spiffs"\)/);
// The I2C expansion header P2 (PINOUT.md 2.6: SDA1 GPIO 4, SCL1 GPIO 3).
const io = read('src/devices/btt_panda_touch/hardware_io_profile.h');
assert.match(io, /\{4, Device::kHardwareIoPinSwitch \| Device::kHardwareIoPinTemperature,\s*"Expansion header P2 GPIO 4 \(SDA1\)"\}/);
assert.match(io, /\{3, Device::kHardwareIoPinSwitch \| Device::kHardwareIoPinTemperature,\s*"Expansion header P2 GPIO 3 \(SCL1\)"\}/);
for (const reserved of [1, 2, 5, 19, 20, 21, 38, 40, 41, 43, 44, 46]) {
  assert.ok(!io.includes(`{${reserved}, Device::`), `GPIO ${reserved} stays reserved`);
}

// --- The other S3 drivers keep their own values -----------------------------
const guition = read('src/devices/guition_esp32_4848s040/device_guition_esp32_4848s040.cpp');
assert.doesNotMatch(guition, /Panda|BTT|btt_panda/,
  'A new device never changes another device driver');

console.log('BTT Panda Touch profile, panel, touch and framebuffer contract: PASS');
