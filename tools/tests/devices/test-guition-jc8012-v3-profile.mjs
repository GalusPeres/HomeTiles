// Issue #44: the Guition JC8012P4A1 V3 is the V2 board (same LCD and JD9365
// init, GSL3680 touch, C6 and pins) with ESP32-P4 v3.2 silicon. A pre-v3
// image restarts a v3 chip right after the bootloader, and the V2 code's
// fixed PLL_F20M DSI clock aborts on v3 silicon. The V3 gets its own image:
// the V2 code built for v3.1 to v3.99 with the clock v3 accepts. The V2
// image, asset and OTA path stay unchanged.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const { getBuildProfile, getReleaseProfile } = require('../../device-catalog.js');

const v2 = getReleaseProfile('guition_jc8012p4a1_v2');
assert.deepEqual([v2.buildProfile, v2.siliconVariant, v2.minimumRevision, v2.maximumRevision,
  v2.installer.acceptsLegacyDescriptor], ['guition_jc8012p4a1_v2', 'pre_v3', 1, 199, true],
'V2 owners keep their image, asset name and legacy policy.');
const v3 = getReleaseProfile('guition_jc8012p4a1_v3');
assert.deepEqual([v3.buildProfile, v3.metadataDeviceKey, v3.define, v3.siliconVariant, v3.minimumRevision,
  v3.maximumRevision, v3.rxVariant, v3.flashSize, v3.installer.acceptsLegacyDescriptor],
['guition_jc8012p4a1_v3', 'guition_jc8012p4a1_v2', 'DEVICE_GUITION_JC8012P4A1_V2', 'post_v3', 301, 399,
  v2.rxVariant, v2.flashSize, false]);
assert.equal(getBuildProfile('guition_jc8012p4a1_v3').define, 'DEVICE_GUITION_JC8012P4A1_V2');
assert.match(v3.installer.label, /JC8012P4A1 V3/);

// Same board options as the V2 apart from the chip generation.
const sketch = read('sketch.yaml');
const fqbn = (name) => sketch.match(new RegExp(`^  ${name}:\\n    fqbn: ([^\\n]+)$`, 'm'))?.[1];
assert.ok(fqbn('guition_jc8012p4a1_v2')?.endsWith('ChipVariant=prev3'));
assert.equal(fqbn('guition_jc8012p4a1_v3'), fqbn('guition_jc8012p4a1_v2').replace('ChipVariant=prev3', 'ChipVariant=postv3'));

// v3 silicon accepts XTAL-based DSI PHY clocks only; pre-v3 keeps PLL_F20M.
const device = read('src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.cpp');
assert.match(device, /#if CONFIG_ESP_REV_MIN_FULL >= 300\n(?:  \/\/[^\n]*\n)*  bus_cfg\.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT;\n#else\n(?:  \/\/[^\n]*\n)*  bus_cfg\.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M;\n#endif/,
  'Each ESP32-P4 generation needs the DSI PHY clock source its HAL accepts.');
assert.doesNotMatch(device, /MIPI_DSI_PHY_CLK_SRC_DEFAULT/,
  'The legacy macro is PLL_F20M on every revision and aborts on v3.');

// The image names the board it runs on; the device key stays the V2's.
const header = read('src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.h');
assert.match(header, /#if CONFIG_ESP_REV_MIN_FULL >= 300\n(?:\/\/[^\n]*\n)*inline constexpr const char\* kDisplayName = "Guition JC8012P4A1 V3";\n#else\ninline constexpr const char\* kDisplayName = "Guition JC8012P4A1 V2";\n#endif/);
assert.match(header, /kProfile\{\n    "guition_jc8012p4a1_v2",\n    kDisplayName,/);
assert.match(header, /#if __has_include\(<sdkconfig\.h>\)\n#include <sdkconfig\.h>\n#endif/,
  'The revision must be known where the name is chosen');
const metadata = read('src/core/firmware/firmware_metadata.cpp');
assert.match(metadata, /#define FW_META_TARGET_DEVICE_KEY "guition_jc8012p4a1_v2"\n(?:\/\/[^\n]*\n)*#if CONFIG_ESP_REV_MIN_FULL >= 300\n#define FW_META_TARGET_DISPLAY_NAME "Guition JC8012P4A1 V3"\n#else\n#define FW_META_TARGET_DISPLAY_NAME "Guition JC8012P4A1 V2"\n#endif/);
const v3Profiles = metadata.slice(metadata.indexOf('#if defined(DEVICE_WAVESHARE_TOUCH_LCD_7B)'),
  metadata.indexOf('#define FW_META_V3_PROFILE 1'));
assert.match(v3Profiles, /defined\(DEVICE_GUITION_JC8012P4A1_V2\)/, 'The V3 descriptor must be 301-399 post_v3');
assert.doesNotMatch(v3Profiles, /DEVICE_GUITION_JC8012P4A1\)/, 'The V1 keeps its fixed clock and pre-v3 image');

// GitHub OTA on a V3 fetches the V3 asset, never the V2 one.
const update = read('src/core/firmware/github_update.cpp');
assert.match(update, /#elif defined\(DEVICE_GUITION_JC8012P4A1_V2\)\n(?:  \/\/[^\n]*\n)*  if \(strcmp\(silicon\.variant, "post_v3"\) == 0\) \{\n    key_out = "guition_jc8012p4a1_v3";\n  \} else if \(strcmp\(silicon\.variant, "pre_v3"\) != 0\) \{/);

const workflow = read('.github/workflows/firmware.yml');
assert.match(workflow, /profile: guition_jc8012p4a1_v3\n            key: guition_jc8012p4a1_v3\n            metadata_key: guition_jc8012p4a1_v2\n            define: DEVICE_GUITION_JC8012P4A1_V2\n            silicon_variant: post_v3/);

console.log('Guition JC8012P4A1 V3 profile contract: PASS');
