// Waveshare 4.3 ESP32-P4 v3.x support (#135) must be a separate, explicit
// profile. The existing pre-v3 profile, asset name and DSI clock stay unchanged.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const { getBuildProfile, getReleaseProfile, validateCatalog } = require('../../device-catalog.js');

const pre = getReleaseProfile('waveshare_touch_lcd_4_3');
assert.deepEqual([pre.buildProfile, pre.siliconVariant, pre.minimumRevision, pre.maximumRevision,
  pre.installer.acceptsLegacyDescriptor], ['waveshare_4_3', 'pre_v3', 1, 199, true],
'Existing pre-v3 4.3 owners must keep their image, asset name and legacy policy.');
const rev3 = getReleaseProfile('waveshare_touch_lcd_4_3_rev3');
assert.deepEqual([rev3.buildProfile, rev3.metadataDeviceKey, rev3.define, rev3.siliconVariant,
  rev3.minimumRevision, rev3.maximumRevision, rev3.installer.acceptsLegacyDescriptor,
  rev3.installer.explicitMetadataKey],
['waveshare_4_3_rev3', 'waveshare_touch_lcd_4_3', 'DEVICE_WAVESHARE_TOUCH_LCD_4_3', 'post_v3', 301, 399, false, true]);
assert.equal(getBuildProfile('waveshare_4_3_rev3').define, 'DEVICE_WAVESHARE_TOUCH_LCD_4_3');

const catalog = JSON.parse(read('tools/device-profiles.json'));
const widened = structuredClone(catalog);
widened.profiles.find((p) => p.key === 'waveshare_touch_lcd_4_3_rev3').minimumRevision = 300;
assert.throws(() => validateCatalog(widened), /unsafe revision range/);

const sketch = read('sketch.yaml');
assert.match(sketch, /^  waveshare_4_3:\n    fqbn: [^\n]*ChipVariant=prev3$/m);
assert.match(sketch, /^  waveshare_4_3_rev3:\n    fqbn: [^\n]*ChipVariant=postv3$/m);
const workflow = read('.github/workflows/firmware.yml');
assert.match(workflow, /profile: waveshare_4_3\n            key: waveshare_touch_lcd_4_3\n            define: DEVICE_WAVESHARE_TOUCH_LCD_4_3\n            silicon_variant: pre_v3/);
assert.match(workflow, /profile: waveshare_4_3_rev3\n            key: waveshare_touch_lcd_4_3_rev3\n            metadata_key: waveshare_touch_lcd_4_3\n            define: DEVICE_WAVESHARE_TOUCH_LCD_4_3\n            silicon_variant: post_v3/);

const metadata = read('src/core/firmware/firmware_metadata.cpp');
const v3Profiles = metadata.slice(metadata.indexOf('#if defined(DEVICE_WAVESHARE_TOUCH_LCD_7B)'),
  metadata.indexOf('#define FW_META_V3_PROFILE 1'));
assert.match(v3Profiles, /defined\(DEVICE_WAVESHARE_TOUCH_LCD_4_3\)/, '4.3 must be a listed v3 profile');

const update = read('src/core/firmware/github_update.cpp');
const selector = update.slice(update.indexOf('bool releaseAssetDeviceKey('), update.indexOf('void logCheckNetworkState('));
assert.match(selector, /#elif defined\(DEVICE_WAVESHARE_TOUCH_LCD_4_3\)\n  if \(strcmp\(silicon\.variant, "post_v3"\) == 0\) \{\n    key_out = "waveshare_touch_lcd_4_3_rev3";/,
  'A v3 4.3 panel must fetch the v3 asset over GitHub OTA.');

const device = read('src/devices/waveshare_touch_lcd_4_3/device_waveshare_touch_lcd_4_3.cpp');
assert.match(device, /#if CONFIG_ESP_REV_MIN_FULL >= 300\n(?:  \/\/[^\n]*\n)*  bus_cfg\.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT;\n#else\n(?:  \/\/[^\n]*\n)*  bus_cfg\.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M;\n#endif/,
  'Each ESP32-P4 generation needs the DSI PHY clock source its HAL accepts.');

console.log('Waveshare 4.3 v3 profile contract: PASS');
