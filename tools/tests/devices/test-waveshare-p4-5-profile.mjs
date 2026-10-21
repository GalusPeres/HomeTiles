// Waveshare ESP32-P4-WIFI6-Touch-LCD-5 (#76): a board description for the
// shared Waveshare ESP32-P4 MIPI-DSI driver. Every value is checked against
// Waveshare's own sources (the LCD-5 BSP 1.0.4 and esp_lcd_hx8394 2.1.0 at
// Waveshare-ESP32-components 23a6012d, the board repository at d144b34d):
// the HX8394 init table, DSI lanes, rate and timing, pins, both ESP32-P4
// silicon builds, the catalog and the Tab5-like presentation.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { getBuildProfile, getReleaseProfile, validateCatalog } from '../../device-catalog.js';
import { DEVICE_PROFILES } from '../../../docs/assets/javascripts/installer-contract.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const exists = (file) => fs.existsSync(path.join(root, file));
const requireMarker = (source, marker, label) => {
  assert.ok(source.includes(marker), `${label} is missing: ${marker}`);
};

const base = 'src/devices/waveshare_touch_lcd_5';
const shared = 'src/devices/waveshare_touch_lcd_5/dsi_driver';
const profile = read(`${base}/profile.h`);
const board = read(`${base}/board.h`);
const boardSource = read(`${base}/board.cpp`);
const deviceHeader = read(`${base}/device_waveshare_touch_lcd_5.h`);
const panelDriver = read(`${base}/vendor/hx8394/esp_lcd_hx8394.c`);
const panelHeader = read(`${base}/vendor/hx8394/esp_lcd_hx8394.h`);
const driver = read(`${shared}/waveshare_p4_dsi.cpp`);
const driverHeader = read(`${shared}/waveshare_p4_dsi.h`);
const boardType = read(`${shared}/waveshare_p4_dsi_board.h`);
const boardSelect = read(`${shared}/waveshare_p4_dsi_board_select.h`);
const sdSource = read(`${shared}/waveshare_p4_dsi_sdmmc.cpp`);
const gt911 = read(`${shared}/vendor/gt911.cpp`);
const gt911Header = read(`${shared}/vendor/gt911.h`);
const i2c = read(`${shared}/vendor/i2c.h`);
const deviceSelect = read('src/devices/device_select.h');

// --- The HX8394 panel: Waveshare's esp_lcd_hx8394 2.1.0 default table, the
// same bytes as the LCD-5 Arduino displays_config.h vendor table.
const expectedTable = [
  ['0xb9', 'ff8394', 0],
  ['0xb1', '480a6a09335471712e45', 0],
  ['0xba', '6103686bb2c0', 0],
  ['0xb2', '0080640c062f', 0],
  ['0xb4', '1c781c781c78010c8675003f1c781c781c78010c86', 0],
  ['0xd3', '000000000000080832100500053213c10001321008000037030707370505370c40', 0],
  ['0xd5', '1818181822232021040506070001020318181818181818181818181818181818181818181818181819191919', 0],
  ['0xd6', '1818191921202322030201000706050418181818181818181818181818181818181818181818181819191818', 0],
  ['0xe0', '0708090d101416132436484a586f768097a5a8b5c66263686f72787f7f0002080d0c0e0f102436484a586f788299a4a0b1c05e5e646b6c737f7f', 0],
  ['0xcc', '0b', 0],
  ['0xc0', '1f73', 0],
  ['0xb6', '6b6b', 0],
  ['0xd4', '02', 0],
  ['0xbd', '01', 0],
  ['0xb1', '00', 0],
  ['0xbd', '00', 0],
  ['0xbf', '408150001afc01', 0],
  ['0x3a', '50', 0],
  ['0x11', '', 200],
  ['0xb2', '0080640c062f00000000c018', 0],
  ['0x29', '', 80],
];
const table = panelDriver.match(
  /static const hx8394_lcd_init_cmd_t vendor_specific_init_code_default\[\] = \{([\s\S]*?)\n\};/);
assert.ok(table, 'HX8394 default init table');
const rows = [...table[1].matchAll(/\{(0x[0-9A-Fa-f]+), \(uint8_t\[\]\)\{([^}]*)\}, (\d+), (\d+)\},/g)]
  .map((m) => {
    const bytes = m[2].split(',').map((b) => b.trim().toLowerCase().replace(/^0x/, ''));
    const count = Number(m[3]);
    // A command without parameters lists one dummy byte with size 0.
    assert.ok(count === bytes.length || (count === 0 && bytes.length === 1), `${m[1]} parameter count`);
    return [m[1].toLowerCase(), count ? bytes.join('') : '', Number(m[4])];
  });
assert.deepEqual(rows, expectedTable, 'HX8394 init table equals Waveshare\'s');
// The pre-table the driver sends first: sleep-out and 120 ms, MADCTL, COLMOD
// (0x55 for RGB565), DSI lanes 0xBA (0x61 = 2 lanes).
const init = panelDriver.slice(panelDriver.indexOf('static esp_err_t panel_hx8394_init(esp_lcd_panel_t *panel)\n{'),
  panelDriver.indexOf('static esp_err_t panel_hx8394_reset(esp_lcd_panel_t *panel)\n{'));
const order = ['LCD_CMD_SLPOUT', 'vTaskDelay(pdMS_TO_TICKS(120))', 'LCD_CMD_MADCTL', 'LCD_CMD_COLMOD',
  'HX8394_CMD_DSI_INT0', 'vendor_specific_init_code_default', 'hx8394->init(panel)'].map((m) => init.indexOf(m));
assert.ok(order.every((at, i) => at > 0 && (i === 0 || at > order[i - 1])), 'HX8394 init order');
requireMarker(panelDriver, '#define HX8394_DSI_2_LANE (0x61)', 'HX8394 2-lane command');
requireMarker(panelDriver, '    case 16: // RGB565\n        hx8394->colmod_val = 0x55;', 'HX8394 RGB565');
requireMarker(panelDriver, 'hx8394->flags.reset_level = panel_dev_config->flags.reset_active_high;', 'HX8394 reset level');
requireMarker(panelDriver, '#if defined(DEVICE_WAVESHARE_TOUCH_LCD_5)', 'HX8394 build guard');
// The standalone I2C sequence (0x45) is gone, as the BSP's
// ESP_LCD_HX8394_SKIP_I2C_INIT selects.
const panelCode = panelDriver.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
assert.doesNotMatch(panelCode, /i2c_bus|i2c_config_t|SKIP_I2C_INIT/, 'no standalone HX8394 I2C sequence');
requireMarker(read(`${base}/vendor/LICENSE-MIT.txt`), 'MIT License', 'HX8394 license');
requireMarker(read(`${base}/vendor/README.md`), '23a6012d5e87d27399cf90fb851cab3689e29159', 'HX8394 source commit');

// --- DSI lanes, rate and timing: the board description equals the BSP and
// the vendored HX8394_720_1280_PANEL_30HZ_DPI_CONFIG.
const boardBody = board.match(/inline constexpr waveshare_p4_dsi::Board kBoard\{([\s\S]*?)\n\};/);
assert.ok(boardBody, 'LCD-5 board description');
const values = boardBody[1].replace(/\/\/.*$/gm, '').split(/[,{}]/).map((v) => v.trim()).filter(Boolean);
assert.deepEqual(values, [
  '"WaveshareTouchLCD5"', 'DeviceWaveshareTouchLCD5Profile::kProfile',
  '"HX8394"', 'createHx8394Panel', '720', '1280', '2', '700', '58000000',
  '20', '20', '40', '4', '10', '24', '3', '2500', 'GPIO_NUM_27', 'true',
  'GPIO_NUM_26', '5000', '10', 'false',
  'GPIO_NUM_7', 'GPIO_NUM_8', '4', 'SDMMC_FREQ_DEFAULT',
], 'LCD-5 board description values');
// The field order these values fill.
const panelFields = [...boardType.match(/struct Panel \{([\s\S]*?)\n\};/)[1]
  .matchAll(/^\s+(?:const char\*|CreatePanelFn|uint16_t|uint8_t|uint32_t|int|gpio_num_t|bool) (\w+);/gm)].map((m) => m[1]);
assert.deepEqual(panelFields, ['controller', 'create', 'width', 'height', 'lane_count', 'lane_bit_rate_mbps',
  'dpi_clock_hz', 'hsync_pulse_width', 'hsync_back_porch', 'hsync_front_porch', 'vsync_pulse_width',
  'vsync_back_porch', 'vsync_front_porch', 'mipi_phy_ldo_channel', 'mipi_phy_ldo_mv', 'reset_gpio',
  'reset_active_high']);
const dpiMacro = panelHeader.slice(panelHeader.indexOf('#define HX8394_720_1280_PANEL_30HZ_DPI_CONFIG'));
for (const marker of ['.dpi_clock_freq_mhz = 58,', '.h_size = 720,', '.v_size = 1280,',
  '.hsync_back_porch = 20,', '.hsync_pulse_width = 20,', '.hsync_front_porch = 40,',
  '.vsync_back_porch = 10,', '.vsync_pulse_width = 4,', '.vsync_front_porch = 24,']) {
  requireMarker(dpiMacro, marker, 'Vendored HX8394 DPI configuration');
}
requireMarker(panelHeader, '.num_data_lanes = 2,', 'Vendored HX8394 bus configuration');
requireMarker(panelHeader, '.lane_bit_rate_mbps = 700,', 'Vendored HX8394 bus configuration');
// The board's panel constructor: the driver's own table on the shared bus.
for (const marker of ['vendor_config.init_cmds = nullptr;', 'vendor_config.mipi_config.lane_num = lane_count;',
  'vendor_config.mipi_config.dpi_config = dpi_config;', 'panel_config.vendor_config = &vendor_config;',
  'return esp_lcd_new_panel_hx8394(io, &panel_config, panel);']) {
  requireMarker(boardSource, marker, 'LCD-5 HX8394 constructor');
}

// --- The shared driver takes every value from the description.
for (const marker of [
  'bus_cfg.num_data_lanes = panel_cfg.lane_count;',
  'bus_cfg.lane_bit_rate_mbps = static_cast<float>(panel_cfg.lane_bit_rate_mbps);',
  'dpi_cfg.dpi_clock_freq_mhz = static_cast<float>(panel_cfg.dpi_clock_hz) / 1000000.0f;',
  'dpi_cfg.video_timing.hsync_pulse_width = panel_cfg.hsync_pulse_width;',
  'dpi_cfg.video_timing.vsync_front_porch = panel_cfg.vsync_front_porch;',
  'dpi_cfg.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;',
  'dpi_cfg.num_fbs = kPanelFrameBufferCount;',
  'panel_dev_cfg.reset_gpio_num = panel_cfg.reset_gpio;',
  'panel_dev_cfg.flags.reset_active_high = panel_cfg.reset_active_high ? 1 : 0;',
  'ldo_cfg.chan_id = panel_cfg.mipi_phy_ldo_channel;',
  'err = panel_cfg.create(g_panel_io, panel_dev_cfg, g_dsi_bus, &dpi_cfg,',
  'g_i2c = DEV_I2C_Init(kActiveBoard.i2c_sda, kActiveBoard.i2c_scl);',
  'g_touch = touch_gt911_init(g_i2c, panel_cfg.width, panel_cfg.height);',
  'WaveshareP4DsiSDMMC.begin(kActiveBoard.sd_ldo_channel, "/sdcard", false,',
  'timer_cfg.freq_hz = backlight_cfg.pwm_hz;',
  'gpio_set_level(panel_cfg.reset_gpio, panel_cfg.reset_active_high ? 1 : 0);',
  'p4_dsi_ui_ppa::Client g_ui_ppa(kActiveBoard.log_name);',
  'p4_dsi_camera_presenter::Transform::Portrait90Or270,',
]) {
  requireMarker(driver, marker, 'Shared Waveshare P4 DSI driver');
}
// The v3 DSI PHY clock, like the 10.1 and JC8012 V3 builds.
assert.match(driver, /#if CONFIG_ESP_REV_MIN_FULL >= 300\n[\s\S]*?bus_cfg\.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT;\n#else\n[\s\S]*?bus_cfg\.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M;\n#endif/);
// A restart holds the panel in reset at its asserted level (high here, low
// on the LCD-X boards), never at a fixed low.
assert.doesNotMatch(driver, /hold_panel_reset_low/);
// The PWM never becomes a static level: duty at most one count below the period.
requireMarker(driver, 'constexpr uint32_t kBacklightMaxDuty = (1u << backlight_cfg.pwm_bits) - 1u;', 'Backlight duty range');
requireMarker(driver, 'uint32_t duty = (brightness_percent * kBacklightMaxDuty) / 100u;', 'Backlight duty');
assert.match(boardSelect, /#if defined\(DEVICE_WAVESHARE_TOUCH_LCD_5\)\n#include "src\/devices\/waveshare_touch_lcd_5\/board\.h"\nnamespace waveshare_p4_dsi \{\ninline constexpr const Board& kActiveBoard = DeviceWaveshareTouchLCD5Board::kBoard;/);
requireMarker(boardSelect, '#error "DEVICE_WAVESHARE_P4_DSI_BOARD needs a board description"', 'Board selection');

// Landscape (x, y) at panel (w - 1 - y, x); the touch mapping is its inverse
// for both rotations (the GT911 reports panel coordinates).
assert.match(driver, /if \(g_rotation & 0x02\) \{\n    dst_x = y;\n    dst_y = logical_w - x - w;\n  \} else \{\n    dst_x = logical_h - y - h;\n    dst_y = x;\n  \}/);
assert.match(driver, /if \(g_rotation & 0x02\) \{\n      out_x = static_cast<int32_t>\(panel_cfg\.height\) - 1 - static_cast<int32_t>\(touch_y\);\n      out_y = static_cast<int32_t>\(touch_x\);\n    \} else \{\n      out_x = static_cast<int32_t>\(touch_y\);\n      out_y = static_cast<int32_t>\(panel_cfg\.width\) - 1 - static_cast<int32_t>\(touch_x\);/);
for (const flipped of [false, true]) {
  const W = 720;
  const H = 1280;
  for (const [x, y] of [[0, 0], [1279, 719], [17, 400], [640, 360]]) {
    // One landscape pixel (w = h = 1) on the panel, then the touch back.
    const px = flipped ? y : W - y - 1;
    const py = flipped ? H - x - 1 : x;
    const back = flipped ? [H - 1 - py, px] : [py, W - 1 - px];
    assert.deepEqual(back, [x, y], `touch inverts the draw (rotation ${flipped ? 2 : 0})`);
  }
}

// --- Pins: I2C, GT911, microSD.
requireMarker(i2c, '#define EXAMPLE_I2C_MASTER_NUM I2C_NUM_0', 'Shared I2C port');
requireMarker(gt911Header, '#define EXAMPLE_PIN_NUM_TOUCH_RST       (GPIO_NUM_NC)', 'GT911 reset not driven');
requireMarker(gt911Header, '#define EXAMPLE_PIN_NUM_TOUCH_INT       (GPIO_NUM_NC)', 'GT911 interrupt not driven');
requireMarker(gt911Header, '#define ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS          (0x5D)', 'GT911 address');
requireMarker(gt911Header, '#define ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS_BACKUP   (0x14)', 'GT911 backup address');
for (const marker of ['.swap_xy = 0,', '.mirror_x = 0,', '.mirror_y = 0,', '.x_max = x_max,', '.y_max = y_max,']) {
  requireMarker(gt911, marker, 'GT911 coordinates in the panel orientation');
}
requireMarker(gt911, '#if defined(DEVICE_WAVESHARE_P4_DSI_BOARD)', 'GT911 build guard');
for (const marker of ['static constexpr int kSdD0Pin = 39;', 'static constexpr int kSdD3Pin = 42;',
  'static constexpr int kSdCmdPin = 44;', 'static constexpr int kSdClkPin = 43;',
  'host.slot = SDMMC_HOST_SLOT_0;', 'host.flags = SDMMC_HOST_FLAG_4BIT;', 'ldo_cfg.voltage_mv = kSdLdoVoltageMv;']) {
  requireMarker(sdSource, marker, 'Shared SDMMC slot 0');
}
// SD at 20 MHz like the hardware-tested Waveshare 8-inch (same slot, pins, LDO).
requireMarker(read('src/devices/waveshare_touch_lcd_8/device_waveshare_touch_lcd_8.cpp'),
  'WaveshareSDMMC.begin("/sdcard", false, SDMMC_FREQ_DEFAULT, 5)', 'the 8-inch SD speed');
// No configurable I/O (touch RST/INT sit on GPIO23/GPIO2).
requireMarker(read(`${base}/hardware_io_profile.h`), 'Device::kNoHardwareIoProfile', 'LCD-5 reserved I/O');
// The optional OV5647 module on the MIPI-CSI connector, like the 4.3-inch:
// the shared OV5647 driver with the 8-inch mode (test-local-camera-boards).
const cameraBlock = deviceSelect.slice(0, deviceSelect.indexOf('#define HOMETILES_LOCAL_CAMERA 1'));
assert.ok(cameraBlock.slice(cameraBlock.lastIndexOf('#if ')).includes('defined(DEVICE_WAVESHARE_TOUCH_LCD_5)'),
  'LCD-5 camera selection');
requireMarker(read('src/video/local_camera/camera_select.h'),
  '#if defined(DEVICE_WAVESHARE_TOUCH_LCD_5)\n#define HOMETILES_CAMERA_SENSOR_OV5647 1\n'
    + '#define HOMETILES_LOCAL_CAMERA_BOARD "src/devices/waveshare_touch_lcd_5/local_camera_board.h"', 'Camera board');
requireMarker(read(`${base}/local_camera_board.h`), 'reset_pin = -1, pwdn_pin = -1', 'Camera pins from Waveshare');
requireMarker(read(`${base}/local_camera_board.cpp`),
  'i2c_master_bus_handle_t bus = DeviceWaveshareTouchLCD5::sharedI2cBus();', 'Camera SCCB on the board bus');

// --- Audio: the LCD-X pins and chips (speaker_board.h), full volume like them.
const speaker = read(`${base}/speaker_board.h`);
for (const marker of ['kI2sMclk = GPIO_NUM_13;', 'kI2sBclk = GPIO_NUM_12;', 'kI2sLrck = GPIO_NUM_10;',
  'kI2sDout = GPIO_NUM_9;', 'kI2sDin = GPIO_NUM_11;', 'kOutput = audio_chips::Output::Es8311;',
  'kCodecAddress = 0x18;', 'kInput = audio_chips::Input::Es7210;', 'kMicAddress = 0x40;',
  'kAmpEnable = GPIO_NUM_53;', 'kAmpActiveHigh = true;', 'kVolumeLimit = 1.0f;']) {
  requireMarker(speaker, marker, 'LCD-5 speaker board');
}
requireMarker(read('src/audio/speaker_select.h'),
  '#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_5)\n#define HOMETILES_SPEAKER_BOARD "src/devices/waveshare_touch_lcd_5/speaker_board.h"', 'Speaker selection');
requireMarker(read(`${base}/speaker_board.cpp`), 'return DeviceWaveshareTouchLCD5::sharedI2cBus();', 'Speaker codec bus');

// --- Tab5-like presentation: the same landscape profile as the Tab5 and
// the Waveshare 7-inch, a direct-PWM backlight floor, flip-only rotation.
const profileValues = (source) => {
  const body = source.match(/inline constexpr Device::Profile kProfile\{([\s\S]*?)\n\};/)[1];
  return body.replace(/\/\/.*$/gm, '').split(',').map((v) => v.trim()).filter(Boolean);
};
const own = profileValues(profile);
const tab5 = profileValues(read('src/devices/m5stacks_tab5/device_m5stacks_tab5.h'));
const lcd7 = profileValues(read('src/devices/waveshare_touch_lcd_7/profile.h'));
assert.deepEqual(own.slice(0, 2), ['"waveshare_touch_lcd_5"', '"Waveshare Touch LCD 5"']);
// Width, height, grid, gap, pad, cells, flush bands; rotation mode and pair.
for (const index of [2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14]) {
  assert.equal(own[index], tab5[index], `profile field ${index} like the Tab5`);
  assert.equal(own[index], lcd7[index], `profile field ${index} like the Waveshare 7-inch`);
}
assert.equal(own[11], '1', 'direct LEDC PWM: raw 1..255 = 1..100 %');
assert.equal(own[15], 'Device::Capabilities{false', 'capabilities');
requireMarker(profile, 'Device::Capabilities{false, false, false, false, true, false, kBuiltinCamera},', 'LCD-5 capabilities');
requireMarker(profile, 'inline constexpr uint32_t kFlashSizeBytes = 32U * 1024U * 1024U;', 'LCD-5 flash');
requireMarker(profile, 'inline constexpr uint32_t kPsramSizeBytes = 32U * 1024U * 1024U;', 'LCD-5 PSRAM');
// The driver checks the description against the profile at compile time.
requireMarker(driver, 'kActiveBoard.profile.screen_width == panel_cfg.height', 'Landscape profile of the portrait panel');
// Upright like the Tab5: the shared presenter draws the upright layout.
const familyBlock = (define) => {
  const end = deviceSelect.indexOf(`#define ${define}\n`);
  return deviceSelect.slice(deviceSelect.lastIndexOf('#if ', end), end);
};
assert.ok(familyBlock('DEVICE_P4_IDF_DSI').includes('defined(DEVICE_WAVESHARE_TOUCH_LCD_5)'));
assert.ok(familyBlock('DEVICE_WAVESHARE_TOUCH_LCD_X').includes('defined(DEVICE_WAVESHARE_TOUCH_LCD_5)'));
assert.equal(familyBlock('DEVICE_WAVESHARE_P4_DSI_BOARD').trim(), '#if defined(DEVICE_WAVESHARE_TOUCH_LCD_5)');
for (const layout of ['DEVICE_LAYOUT_1024X600', 'DEVICE_LAYOUT_480X480']) {
  assert.ok(!familyBlock(layout).includes('LCD_5'), `the 1280 x 720 layout, not ${layout}`);
}
// The 5-inch is added to the upright lists one by one; no other entry changes.
for (const file of ['src/tiles/config/grid_layout.h', 'src/devices/device.cpp']) {
  requireMarker(read(file), 'defined(DEVICE_GUITION_JC4880P443_PORTRAIT) || \\\n    defined(DEVICE_WAVESHARE_TOUCH_LCD_5)\n',
    `Upright list in ${file}`);
}
requireMarker(driver, 'g_camera_presenter.drawUpright(x, y, w, h, data, g_rotation);', 'Upright draw');

// --- The device API is the shared driver's, complete like the LCD-X boards'.
const declared = [...driverHeader.matchAll(/^[\w:&*]+[ &*]+(\w+)\(/gm)].map((m) => m[1]);
const exported = [...deviceHeader.matchAll(/^using waveshare_p4_dsi::(\w+);/gm)].map((m) => m[1]);
assert.deepEqual(exported, declared, 'every shared driver function is the device API');
const lcd7Api = [...read('src/devices/waveshare_touch_lcd_7/device_waveshare_touch_lcd_7.h')
  .matchAll(/^[\w:&*]+[ &*]+(\w+)\(/gm)].map((m) => m[1]);
assert.deepEqual([...declared].sort(), [...lcd7Api].sort(), 'the same API as the Waveshare 7-inch');
for (const name of declared) {
  assert.match(driver, new RegExp(`^[\\w:&*]+[ &*]+waveshare_p4_dsi::${name}\\(`, 'm'), `${name} is defined`);
}

// --- Both silicon builds and their catalog entries.
const sketch = read('sketch.yaml');
assert.match(sketch, /^  waveshare_5:\n    fqbn: esp32:esp32:esp32p4:[^\n]*FlashSize=32M,PartitionScheme=app13M_data7M_32MB,[^\n]*PSRAM=enabled,[^\n]*ChipVariant=prev3$/m);
assert.match(sketch, /^  waveshare_5_rev3:\n    fqbn: esp32:esp32:esp32p4:[^\n]*FlashSize=32M,PartitionScheme=app13M_data7M_32MB,[^\n]*PSRAM=enabled,[^\n]*ChipVariant=postv3$/m);
const pre = getReleaseProfile('waveshare_touch_lcd_5');
const rev3 = getReleaseProfile('waveshare_touch_lcd_5_rev3');
assert.deepEqual([pre.buildProfile, pre.metadataDeviceKey, pre.define, pre.chipFamily, pre.flashSize,
  pre.siliconVariant, pre.minimumRevision, pre.maximumRevision, pre.rxVariant, pre.installer.status,
  pre.installer.acceptsLegacyDescriptor],
['waveshare_5', 'waveshare_touch_lcd_5', 'DEVICE_WAVESHARE_TOUCH_LCD_5', 'ESP32-P4', 32 * 1024 * 1024,
  'pre_v3', 1, 199, 'repo-a8204', 'validation-pending', true]);
assert.deepEqual([rev3.buildProfile, rev3.metadataDeviceKey, rev3.define, rev3.chipFamily, rev3.flashSize,
  rev3.siliconVariant, rev3.minimumRevision, rev3.maximumRevision, rev3.rxVariant, rev3.installer.status,
  rev3.installer.acceptsLegacyDescriptor],
['waveshare_5_rev3', 'waveshare_touch_lcd_5', 'DEVICE_WAVESHARE_TOUCH_LCD_5', 'ESP32-P4', 32 * 1024 * 1024,
  'post_v3', 301, 399, 'repo-a8204', 'validation-pending', false]);
assert.match(pre.installer.hardwareCheck, /ESP32-P4-WIFI6-Touch-LCD-5 with a 720x1280 HX8394 panel.*before v3\.0/);
assert.match(rev3.installer.hardwareCheck, /ESP32-P4-WIFI6-Touch-LCD-5 with a 720x1280 HX8394 panel.*v3\.1 or newer\. Revision v3\.0 is unsupported/);
assert.equal(getBuildProfile('waveshare_5_rev3').define, 'DEVICE_WAVESHARE_TOUCH_LCD_5');
for (const key of ['waveshare_touch_lcd_5', 'waveshare_touch_lcd_5_rev3']) {
  const installer = DEVICE_PROFILES.find((device) => device.key === key);
  assert.ok(installer, `${key} installer profile`);
  assert.equal(installer.metadataDeviceKey, 'waveshare_touch_lcd_5');
}
// The catalog accepts the v3 image only with this firmware key and define.
const catalog = JSON.parse(read('tools/device-profiles.json'));
const misuse = structuredClone(catalog);
misuse.profiles.find((p) => p.key === 'waveshare_touch_lcd_5_rev3').metadataDeviceKey = 'waveshare_touch_lcd_7';
assert.throws(() => validateCatalog(misuse), /unsupported v3\.x target/);
const workflow = read('.github/workflows/firmware.yml');
assert.match(workflow, /profile: waveshare_5\n            key: waveshare_touch_lcd_5\n            define: DEVICE_WAVESHARE_TOUCH_LCD_5\n            silicon_variant: pre_v3/);
assert.match(workflow, /profile: waveshare_5_rev3\n            key: waveshare_touch_lcd_5_rev3\n            metadata_key: waveshare_touch_lcd_5\n            define: DEVICE_WAVESHARE_TOUCH_LCD_5\n            silicon_variant: post_v3/);
const metadata = read('src/core/firmware/firmware_metadata.cpp');
requireMarker(metadata, '#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_5)\n#define FW_META_TARGET_DEVICE_KEY "waveshare_touch_lcd_5"\n#define FW_META_TARGET_DISPLAY_NAME "Waveshare Touch LCD 5"', 'Firmware identity');
const v3Profiles = metadata.slice(metadata.indexOf('// Only these profiles select the MIPI DSI clock'),
  metadata.indexOf('#define FW_META_V3_PROFILE 1'));
requireMarker(v3Profiles, 'defined(DEVICE_WAVESHARE_P4_DSI_BOARD)', 'v3 metadata for the shared driver');
requireMarker(read('src/core/firmware/github_update.cpp'),
  '#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_5)\n  if (strcmp(silicon.variant, "post_v3") == 0) {\n    key_out = "waveshare_touch_lcd_5_rev3";', 'OTA asset of the v3 build');
requireMarker(read('src/web/setup/web_config.cpp'), '#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_5)\n  return "Waveshare_LCD5_Config";', 'Setup AP name');
requireMarker(read('src/devices/active_device.h'),
  '#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_5)\n#include "src/devices/waveshare_touch_lcd_5/device_waveshare_touch_lcd_5.h"\nnamespace DeviceImpl = DeviceWaveshareTouchLCD5;', 'Device selection');

// --- Documentation: support status and the open hardware checks.
const readme = read(`${base}/README.md`);
for (const marker of ['ESP32-P4-WIFI6-Touch-LCD-5', '`waveshare_5`', '`waveshare_5_rev3`',
  'CLK GPIO18, CMD GPIO19, D0-D3 GPIO14-17, C6 reset GPIO54', 'TP_RST', 'GPIO23', 'TP_INT', 'GPIO2',
  'Not confirmed (hardware check pending)']) {
  requireMarker(readme, marker, 'LCD-5 README');
}
const docs = read('docs/index.md');
requireMarker(docs, '| Waveshare LCD-5 (P4 pre-v3) | 5" / 1280×720 | <span class="ht-device-state">Pending', 'Device list');
requireMarker(docs, '| Waveshare LCD-5 (P4 v3.x) | 5" / 1280×720 | <span class="ht-device-state">Experimental', 'Device list');
requireMarker(docs, 'https://github.com/GalusPeres/HomeTiles/issues/76', 'Device list issue link');
requireMarker(read('BOARD_SETTINGS.md'), '## Waveshare Touch LCD 5', 'Board settings');

console.log('Waveshare Touch LCD 5 profile contract: PASS');
