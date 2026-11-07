// The Waveshare ESP32-S3-Touch-LCD-7 uses an 800x480 RGB panel. Three
// settings keep its picture stable on Arduino-ESP32 3.3.7 (ESP-IDF 5.5.2):
// data is launched on the falling PCLK edge, the bounce buffer spans 20 lines,
// and the per-VSYNC GDMA restart is masked because IDF's bounce-buffer index is
// not re-synchronised by that restart (IDFGH-18263, fixed after 5.5.2).
//
// The profile drives esp_lcd directly. ESP32_Display_Panel must not return:
// it depends on ESP32_IO_Expander, whose port sources link the legacy I2C
// driver and abort at boot because the touch bus uses the new driver.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), 'utf8').replace(/\r\n/g, '\n');
}

const port = read('src/devices/waveshare_s3_touch_lcd_7/waveshare_lcd_port.h');
const device = read(
  'src/devices/waveshare_s3_touch_lcd_7/device_waveshare_s3_touch_lcd_7.cpp');
// Comments must not defeat the configuration checks (or hide a library return).
const deviceCode = device.replace(/\/\/[^\n]*/g, '');

assert.match(
  port,
  /#define EXAMPLE_LCD_RGB_TIMING_FREQ_HZ\s+\(16 \* 1000 \* 1000\)/,
  'The panel must keep the vendor 16 MHz PCLK',
);
assert.match(
  port,
  /#define EXAMPLE_LCD_RGB_BOUNCE_BUFFER_SIZE\s+\(EXAMPLE_LCD_WIDTH \* 20\)/,
  'The bounce buffer must span 20 lines for the GDMA refill budget',
);

// The panel latches on the rising edge; the peripheral must launch data on
// the falling edge. The remaining RGB timing flags stay at their idle levels.
assert.match(
  deviceCode,
  /config\.timings\.flags\.pclk_active_neg = 1;/,
  'pclk_active_neg must be enabled for the ST7262/EK9716 panel',
);
for (const flag of ['hsync_idle_low', 'vsync_idle_low', 'de_idle_high', 'pclk_idle_high']) {
  assert.match(
    deviceCode,
    new RegExp(`config\\.timings\\.flags\\.${flag} = 0;`),
    `${flag} must stay at the vendor level`,
  );
}
assert.match(
  deviceCode,
  /config\.bounce_buffer_size_px = EXAMPLE_LCD_RGB_BOUNCE_BUFFER_SIZE;/,
  'The bounce buffer size must be set before esp_lcd_new_rgb_panel()',
);
assert.match(
  deviceCode,
  /esp_err_t err = esp_lcd_new_rgb_panel\(&config, &g_panel\);/,
  'The panel must be created through esp_lcd_new_rgb_panel()',
);
assert.match(
  deviceCode,
  /if \(err != ESP_OK\)[\s\S]*?esp_lcd_new_rgb_panel failed/,
  'esp_lcd_new_rgb_panel() failure must not be ignored',
);
assert.match(
  deviceCode,
  /err = esp_lcd_panel_reset\(g_panel\);/,
  'the panel must be reset before esp_lcd_panel_init()',
);
assert.match(
  deviceCode,
  /err = esp_lcd_panel_init\(g_panel\);/,
  'esp_lcd_panel_init() must be called on the created panel',
);
assert.match(
  deviceCode,
  /if \(err != ESP_OK\)[\s\S]*?esp_lcd_panel_init failed/,
  'esp_lcd_panel_init() failure must not be ignored',
);

const begin = device.indexOf('err = esp_lcd_panel_init(g_panel);');
const mask = device.indexOf('disableVsyncRestart();');
assert.ok(begin >= 0 && mask > begin,
          'The VSYNC restart must be masked after the panel has begun');
assert.match(
  device,
  /lcd_ll_enable_interrupt\(&LCD_CAM, LCD_LL_EVENT_RGB, false\);/,
  'Masking must disable the LCD VSYNC interrupt, not the GDMA EOF interrupt',
);

// No library indirection: the profile must not include or name the display
// panel library, and the sketch profile must not install its I2C dependency.
assert.doesNotMatch(port, /esp_display_panel|esp_panel::/);
assert.doesNotMatch(deviceCode, /esp_display_panel|esp_panel::|LCD\*|BusRGB/);
const sketch = read('sketch.yaml');
const profile = sketch.match(
  /^  waveshare_s3_touch_lcd_7:\n([\s\S]*?)(?=\n  [a-z][a-z0-9_]*:\n)/m,
);
assert.ok(profile, 'the Waveshare S3 7" profile is present in sketch.yaml');
assert.doesNotMatch(
  profile[1],
  /ESP32_Display_Panel|ESP32_IO_Expander|esp-lib-utils/,
  'the profile must not depend on the display panel or I2C expander libraries',
);

console.log('Waveshare ESP32-S3-Touch-LCD-7 display contract OK');
