import assert from 'node:assert/strict';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const {
  parseFirmwareMetadata,
  readManualDeviceSelection,
  resolveReleaseDevice,
} = require('../../../release-helper/import-latest-arduino-build.js');

const releaseTargets = new Map([
  ['DEVICE_M5STACKS_TAB5', { key: 'm5stacks_tab5', siliconVariant: 'pre_v3' }],
  ['DEVICE_WAVESHARE_4B', { key: 'waveshare_4b', siliconVariant: 'pre_v3' }],
  ['DEVICE_WAVESHARE_TOUCH_LCD_4_3', {
    key: 'waveshare_touch_lcd_4_3',
    siliconVariant: 'pre_v3',
  }],
  ['DEVICE_WAVESHARE_TOUCH_LCD_7', {
    key: 'waveshare_touch_lcd_7',
    siliconVariant: 'pre_v3',
  }],
  ['DEVICE_WAVESHARE_TOUCH_LCD_7B', {
    key: 'waveshare_touch_lcd_7b',
    siliconVariant: null,
  }],
  ['DEVICE_WAVESHARE_TOUCH_LCD_8', {
    key: 'waveshare_touch_lcd_8',
    siliconVariant: 'pre_v3',
  }],
  ['DEVICE_WAVESHARE_TOUCH_LCD_10_1', {
    key: 'waveshare_touch_lcd_10_1',
    siliconVariant: null,
  }],
  ['DEVICE_WAVESHARE_S3_TOUCH_LCD_4', {
    key: 'waveshare_s3_touch_lcd_4',
    siliconVariant: 'default',
  }],
  ['DEVICE_WAVESHARE_S3_TOUCH_LCD_4B', {
    key: 'waveshare_s3_touch_lcd_4b',
    siliconVariant: 'default',
  }],
  ['DEVICE_GUITION_JC8012P4A1', {
    key: 'guition_jc8012p4a1',
    siliconVariant: 'pre_v3',
  }],
  ['DEVICE_GUITION_JC8012P4A1_V2', {
    key: 'guition_jc8012p4a1_v2',
    siliconVariant: 'pre_v3',
  }],
  ['DEVICE_GUITION_JC1060P470C', {
    key: 'guition_jc1060p470c',
    siliconVariant: 'pre_v3',
  }],
  ['DEVICE_GUITION_JC1060P470C_V2', {
    key: 'guition_jc1060p470c_v2',
    siliconVariant: 'pre_v3',
  }],
  ['DEVICE_GUITION_JC4880P443_PORTRAIT', {
    key: 'guition_jc4880p443_portrait',
    siliconVariant: 'pre_v3',
  }],
  ['DEVICE_GUITION_ESP32_4848S040', {
    key: 'guition_esp32_4848s040',
    siliconVariant: 'default',
  }],
]);

function selectorSource(...defines) {
  const enabled = defines.map((define) => `#define ${define}`).join('\n');
  return `
#pragma once
#if !defined(HOMETILES_CI_TARGET)
${enabled}
#endif
#if defined(DEVICE_WAVESHARE_4B)
#define DEVICE_LAYOUT_480X480
#endif
`;
}

function writeString(buffer, offset, maxLength, value) {
  const encoded = Buffer.from(value, 'utf8');
  assert.ok(encoded.length < maxLength);
  encoded.copy(buffer, offset);
}

function firmwareImage(
  deviceKey,
  { appOffset = 0, siliconVariant = 'default', minimumRevision = 0, maximumRevision = 0xffff } = {},
) {
  const descriptorOffset = appOffset + 24 + 8 + 256;
  const deviceDescriptorLength = 4 + 32 + 32 + 32;
  const siliconOffset = descriptorOffset + deviceDescriptorLength;
  const image = Buffer.alloc(siliconOffset + 4 + 2 + 2 + 16);
  image[appOffset] = 0xe9;
  image.writeUInt32LE(0xabcd5432, appOffset + 24 + 8);
  image.writeUInt32LE(0x44565034, descriptorOffset);
  writeString(image, descriptorOffset + 4, 32, 'esp32_p4_homeassistant_display');
  writeString(image, descriptorOffset + 4 + 32, 32, deviceKey);
  writeString(image, descriptorOffset + 4 + 32 + 32, 32, 'Regression fixture');
  image.writeUInt32LE(0x53525634, siliconOffset);
  image.writeUInt16LE(minimumRevision, siliconOffset + 4);
  image.writeUInt16LE(maximumRevision, siliconOffset + 6);
  writeString(image, siliconOffset + 8, 16, siliconVariant);
  return image;
}

function withoutSiliconMetadata(image, appOffset = 0) {
  const siliconOffset = appOffset + 24 + 8 + 256 + 4 + 32 + 32 + 32;
  image.writeUInt32LE(0, siliconOffset);
  return image;
}

for (const [define, target] of releaseTargets) {
  const selection = readManualDeviceSelection(selectorSource(define));
  assert.equal(selection.define, define);
  assert.equal(selection.key, target.key);

  if (target.siliconVariant === null) {
    assert.ok(selection.siliconVariants instanceof Map);
    continue;
  }

  assert.equal(selection.expectedSiliconVariant, target.siliconVariant);
  const metadataOptions = target.siliconVariant === 'pre_v3'
    ? { siliconVariant: 'pre_v3', minimumRevision: 1, maximumRevision: 199 }
    : {};
  const metadata = parseFirmwareMetadata(firmwareImage(target.key, metadataOptions));
  assert.equal(resolveReleaseDevice(selection, metadata).key, target.key);
}

assert.throws(
  () => readManualDeviceSelection(selectorSource()),
  /refusing to assume Waveshare 4B/,
);
assert.throws(
  () => readManualDeviceSelection(selectorSource('DEVICE_M5STACKS_TAB5', 'DEVICE_WAVESHARE_4B')),
  /Multiple device targets/,
);
assert.throws(
  () => readManualDeviceSelection(selectorSource('DEVICE_LAYOUT_TEST_1024X600')),
  /layout-test target/,
);
assert.throws(
  () => readManualDeviceSelection(selectorSource('DEVICE_UNKNOWN_BOARD')),
  /Unsupported release device target/,
);

const jc1060V2Selection = readManualDeviceSelection(
  selectorSource('DEVICE_GUITION_JC1060P470C_V2'),
);
const jc1060V2Metadata = parseFirmwareMetadata(firmwareImage('guition_jc1060p470c_v2', {
  siliconVariant: 'pre_v3',
  minimumRevision: 1,
  maximumRevision: 199,
}));
assert.equal(resolveReleaseDevice(jc1060V2Selection, jc1060V2Metadata).key, 'guition_jc1060p470c_v2');
assert.throws(
  () => resolveReleaseDevice(jc1060V2Selection, parseFirmwareMetadata(firmwareImage('waveshare_4b'))),
  /Latest Arduino build is for waveshare_4b/,
);
assert.throws(
  () => resolveReleaseDevice(
    jc1060V2Selection,
    parseFirmwareMetadata(withoutSiliconMetadata(firmwareImage('guition_jc1060p470c_v2'))),
  ),
  /no silicon-revision metadata/,
);
assert.throws(
  () => resolveReleaseDevice(
    jc1060V2Selection,
    parseFirmwareMetadata(firmwareImage('guition_jc1060p470c_v2')),
  ),
  /expected pre_v3, got default/,
);
assert.throws(
  () => resolveReleaseDevice(
    jc1060V2Selection,
    parseFirmwareMetadata(firmwareImage('guition_jc1060p470c_v2', {
      siliconVariant: 'pre_v3',
      minimumRevision: 1,
      maximumRevision: 299,
    })),
  ),
  /Unsafe DEVICE_GUITION_JC1060P470C_V2 pre-v3 revision range/,
);
assert.throws(
  () => resolveReleaseDevice(
    jc1060V2Selection,
    parseFirmwareMetadata(firmwareImage('guition_jc1060p470c_v2', {
      siliconVariant: 'pre_v3',
      minimumRevision: 0,
      maximumRevision: 199,
    })),
  ),
  /Unsafe DEVICE_GUITION_JC1060P470C_V2 pre-v3 revision range/,
);

const guitionS3Selection = readManualDeviceSelection(
  selectorSource('DEVICE_GUITION_ESP32_4848S040'),
);
assert.throws(
  () => resolveReleaseDevice(
    guitionS3Selection,
    parseFirmwareMetadata(firmwareImage('guition_esp32_4848s040', {
      siliconVariant: 'pre_v3',
      maximumRevision: 299,
    })),
  ),
  /expected default, got pre_v3/,
);

const waveshare7BSelection = readManualDeviceSelection(
  selectorSource('DEVICE_WAVESHARE_TOUCH_LCD_7B'),
);
const preV3Metadata = parseFirmwareMetadata(
  firmwareImage('waveshare_touch_lcd_7b', {
    siliconVariant: 'pre_v3',
    minimumRevision: 1,
    maximumRevision: 199,
  }),
);
assert.equal(resolveReleaseDevice(waveshare7BSelection, preV3Metadata).key, 'waveshare_touch_lcd_7b');
assert.throws(
  () => resolveReleaseDevice(
    waveshare7BSelection,
    parseFirmwareMetadata(firmwareImage('waveshare_touch_lcd_7b', {
      siliconVariant: 'pre_v3',
      minimumRevision: 1,
      maximumRevision: 299,
    })),
  ),
  /Unsafe Waveshare 7B pre-v3 revision range/,
);

const rev3Metadata = parseFirmwareMetadata(
  firmwareImage('waveshare_touch_lcd_7b', {
    siliconVariant: 'rev3_1',
    minimumRevision: 301,
    maximumRevision: 301,
  }),
);
assert.equal(
  resolveReleaseDevice(waveshare7BSelection, rev3Metadata).key,
  'waveshare_touch_lcd_7b_rev3_1',
);

const waveshare101Selection = readManualDeviceSelection(
  selectorSource('DEVICE_WAVESHARE_TOUCH_LCD_10_1'),
);
assert.equal(
  resolveReleaseDevice(waveshare101Selection, parseFirmwareMetadata(
    firmwareImage('waveshare_touch_lcd_10_1', { siliconVariant: 'pre_v3', minimumRevision: 1, maximumRevision: 199 }),
  )).key,
  'waveshare_touch_lcd_10_1',
  'The existing pre-v3 10.1 image keeps its release asset name.',
);
assert.equal(
  resolveReleaseDevice(waveshare101Selection, parseFirmwareMetadata(
    firmwareImage('waveshare_touch_lcd_10_1', { siliconVariant: 'post_v3', minimumRevision: 301, maximumRevision: 399 }),
  )).key,
  'waveshare_touch_lcd_10_1_rev3',
);
for (const [minimumRevision, maximumRevision] of [[300, 399], [301, 301], [1, 399]]) {
  assert.throws(
    () => resolveReleaseDevice(waveshare101Selection, parseFirmwareMetadata(
      firmwareImage('waveshare_touch_lcd_10_1', { siliconVariant: 'post_v3', minimumRevision, maximumRevision }),
    )),
    /Unsafe DEVICE_WAVESHARE_TOUCH_LCD_10_1 post_v3 revision range/,
  );
}
assert.throws(
  () => resolveReleaseDevice(waveshare101Selection, parseFirmwareMetadata(
    firmwareImage('waveshare_touch_lcd_10_1', { siliconVariant: 'rev3_1', minimumRevision: 301, maximumRevision: 301 }),
  )),
  /Unsupported DEVICE_WAVESHARE_TOUCH_LCD_10_1 silicon variant/,
);

const legacy7BImage = firmwareImage('waveshare_touch_lcd_7b', {
  siliconVariant: 'pre_v3',
  minimumRevision: 1,
  maximumRevision: 199,
});
assert.throws(
  () => resolveReleaseDevice(
    waveshare7BSelection,
    parseFirmwareMetadata(withoutSiliconMetadata(legacy7BImage)),
  ),
  /no silicon-revision metadata/,
);
assert.throws(
  () => resolveReleaseDevice(
    waveshare7BSelection,
    parseFirmwareMetadata(firmwareImage('waveshare_touch_lcd_7b', {
      siliconVariant: 'rev3_1',
      minimumRevision: 301,
      maximumRevision: 399,
    })),
  ),
  /Unsafe Waveshare 7B v3.1 revision range/,
);
assert.throws(
  () => parseFirmwareMetadata(firmwareImage('waveshare_touch_lcd_7b', {
    siliconVariant: 'pre_v3',
    minimumRevision: 299,
    maximumRevision: 100,
  })),
  /invalid silicon revision range/,
);

const factoryMetadata = parseFirmwareMetadata(
  firmwareImage('guition_jc8012p4a1_v2', { appOffset: 0x10000 }),
  0x10000,
);
assert.equal(factoryMetadata.deviceKey, 'guition_jc8012p4a1_v2');

console.log('Arduino build import device-selection contract: PASS');
