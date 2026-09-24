// Waveshare 10.1 ESP32-P4 v3.x support must be a separate, explicit profile.
// The existing pre-v3 profile, asset name and DSI clock source stay unchanged.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const { getBuildProfile, getReleaseProfile, validateCatalog } = require('../../device-catalog.js');

const pre = getReleaseProfile('waveshare_touch_lcd_10_1');
assert.deepEqual([pre.buildProfile, pre.siliconVariant, pre.minimumRevision, pre.maximumRevision,
  pre.installer.acceptsLegacyDescriptor], ['waveshare_10_1', 'pre_v3', 1, 199, true],
'Existing pre-v3 10.1 owners must keep their image, asset name and legacy policy.');
const rev3 = getReleaseProfile('waveshare_touch_lcd_10_1_rev3');
assert.deepEqual([rev3.buildProfile, rev3.metadataDeviceKey, rev3.define, rev3.siliconVariant,
  rev3.minimumRevision, rev3.maximumRevision, rev3.installer.acceptsLegacyDescriptor],
['waveshare_10_1_rev3', 'waveshare_touch_lcd_10_1', 'DEVICE_WAVESHARE_TOUCH_LCD_10_1', 'post_v3', 301, 399, false]);
assert.equal(getBuildProfile('waveshare_10_1_rev3').define, 'DEVICE_WAVESHARE_TOUCH_LCD_10_1');

const catalog = JSON.parse(read('tools/device-profiles.json'));
const misuse = structuredClone(catalog);
misuse.profiles.find((p) => p.key === 'waveshare_touch_lcd_8').siliconVariant = 'post_v3';
assert.throws(() => validateCatalog(misuse), /unsafe revision range|unsupported v3\.x target/);
const widened = structuredClone(catalog);
widened.profiles.find((p) => p.key === 'waveshare_touch_lcd_10_1_rev3').minimumRevision = 300;
assert.throws(() => validateCatalog(widened), /unsafe revision range/);

const sketch = read('sketch.yaml');
assert.match(sketch, /^  waveshare_10_1:\n    fqbn: [^\n]*ChipVariant=prev3$/m);
assert.match(sketch, /^  waveshare_10_1_rev3:\n    fqbn: [^\n]*ChipVariant=postv3$/m);
const workflow = read('.github/workflows/firmware.yml');
assert.match(workflow, /profile: waveshare_10_1\n            key: waveshare_touch_lcd_10_1\n            define: DEVICE_WAVESHARE_TOUCH_LCD_10_1\n            silicon_variant: pre_v3/);
assert.match(workflow, /profile: waveshare_10_1_rev3\n            key: waveshare_touch_lcd_10_1_rev3\n            metadata_key: waveshare_touch_lcd_10_1\n            define: DEVICE_WAVESHARE_TOUCH_LCD_10_1\n            silicon_variant: post_v3/);

const metadata = read('src/core/firmware/firmware_metadata.cpp');
const branch = metadata.indexOf('#elif CONFIG_ESP_REV_MIN_FULL >= 300 && defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)');
assert.ok(branch > 0, '10.1 v3 descriptor must be an exact-profile branch');
const branchText = metadata.slice(branch, metadata.indexOf('#else', branch));
for (const marker of ['#define FW_META_SILICON_VARIANT "post_v3"', '#define FW_META_SILICON_MIN_REV 301',
  '#define FW_META_SILICON_MAX_REV 399', '#if CONFIG_ESP_REV_MIN_FULL != 301 || CONFIG_ESP_REV_MAX_FULL != 399']) {
  assert.ok(branchText.includes(marker), `missing ${marker}`);
}
assert.ok(metadata.includes('#error "Every ESP32-P4 build must target one unambiguous silicon generation"'),
  'Unlisted postv3 builds must still fail to compile.');

const update = read('src/core/firmware/github_update.cpp');
const selector = update.slice(update.indexOf('bool releaseAssetDeviceKey('), update.indexOf('void logCheckNetworkState('));
assert.match(selector, /#elif defined\(DEVICE_WAVESHARE_TOUCH_LCD_10_1\)\n  if \(strcmp\(silicon\.variant, "post_v3"\) == 0\) \{\n    key_out = "waveshare_touch_lcd_10_1_rev3";/);

const device = read('src/devices/waveshare_touch_lcd_10_1/device_waveshare_touch_lcd_10_1.cpp');
assert.match(device, /#if CONFIG_ESP_REV_MIN_FULL >= 300\n(?:  \/\/[^\n]*\n)*  bus_cfg\.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT;\n#else\n(?:  \/\/[^\n]*\n)*  bus_cfg\.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M;\n#endif/,
  'Each ESP32-P4 generation needs the DSI PHY clock source its HAL accepts.');

console.log('Waveshare 10.1 v3 profile contract: PASS');
