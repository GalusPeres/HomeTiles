import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

function read(relativePath) {
  return fs.readFileSync(path.join(root, relativePath), 'utf8')
    .replace(/\r\n?/g, '\n');
}

function requireMarker(source, marker, label) {
  if (!source.includes(marker)) {
    throw new Error(`${label} is missing: ${marker}`);
  }
}

const catalog = JSON.parse(read('tools/device-profiles.json'));
const profile = catalog.profiles.find((item) => item.key === 'guition_jc1060p470c_v3');
if (!profile ||
    profile.buildProfile !== 'guition_jc1060p470c_v3' ||
    profile.define !== 'DEVICE_GUITION_JC1060P470C_V3' ||
    profile.metadataDeviceKey !== 'guition_jc1060p470c_v3' ||
    profile.chipFamily !== 'ESP32-P4' ||
    profile.flashSize !== 16 * 1024 * 1024 ||
    profile.siliconVariant !== 'post_v3' ||
    profile.minimumRevision !== 301 ||
    profile.maximumRevision !== 399 ||
    profile.installer?.status !== 'validation-pending' ||
    profile.installer?.acceptsLegacyDescriptor !== false) {
  throw new Error('JC1060P470C V3 release profile is incomplete or unsafe');
}

const deviceSelect = read('src/devices/device_select.h');
const activeDevice = read('src/devices/active_device.h');
const v3DeviceHeader = read(
  'src/devices/guition_jc1060p470c_v3/device_guition_jc1060p470c_v3.h');
const displayConfig = read(
  'src/devices/guition_jc1060p470c_v3/vendor/displays_config.h');
const panelDriver = read(
  'src/devices/guition_jc1060p470c_v3/vendor/esp_lcd_jd9165.c');
const deviceDriver = read(
  'src/devices/guition_jc1060p470c_v3/device_guition_jc1060p470c_v3.cpp');
const v2Sources = [
  'device_guition_jc1060p470c_v2.cpp',
  'local_camera_board.cpp',
  'local_camera_board.h',
  'vendor/displays_config.h',
  'vendor/esp_lcd_jd9165.c',
  'vendor/gt911.cpp',
  'vendor/guition_sdmmc.cpp',
  'vendor/guition_sdmmc.h',
  'vendor/i2c.cpp',
  'vendor/touch.cpp',
].map(file => read(`src/devices/guition_jc1060p470c_v2/${file}`));
const metadata = read('src/core/firmware/firmware_metadata.cpp');
const webConfig = read('src/web/setup/web_config.cpp');
const sketch = read('sketch.yaml');

for (const [source, marker, label] of [
  [deviceSelect, 'defined(DEVICE_GUITION_JC1060P470C_V3)', 'device selection'],
  [deviceSelect, 'defined(DEVICE_GUITION_JC1060P470C_V3) ||', 'P4 DSI registration'],
  [activeDevice, 'namespace DeviceImpl = DeviceGuitionJC1060P470CV3;', 'V3 device interface'],
  [v3DeviceHeader, 'namespace DeviceGuitionJC1060P470CV3', 'V3 device namespace'],
  [v3DeviceHeader, '"guition_jc1060p470c_v3"', 'V3 runtime identity'],
  [v3DeviceHeader, 'Device::kNoHardwareIoProfile', 'unverified expansion GPIOs'],
  [v3DeviceHeader, 'Device::Capabilities{false, false, false, false, false, false, kBuiltinCamera}',
    'local camera capability'],
  [displayConfig, '52000000', 'V2 pixel clock'],
  [displayConfig, '750', 'V2 DSI lane rate'],
  [panelDriver, 'defined(DEVICE_GUITION_JC1060P470C_V3)', 'V3 JD9165 init sequence'],
  [deviceDriver, 'src/devices/guition_jc1060p470c_v3/vendor/displays_config.h',
    'V3-local panel dependencies'],
  [deviceDriver, 'bool DeviceGuitionJC1060P470CV3::init()', 'V3-local panel lifecycle'],
  [deviceDriver, 'defined(DEVICE_GUITION_JC1060P470C_V3)', 'V3-only driver guard'],
  [deviceDriver, 'bus_cfg.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT;', 'production-silicon DSI clock'],
  [deviceDriver, 'bus_cfg.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M;', 'pre-v3 DSI clock'],
  [metadata, 'FW_META_TARGET_DEVICE_KEY "guition_jc1060p470c_v3"', 'V3 OTA identity'],
  [metadata, 'defined(DEVICE_GUITION_JC1060P470C_V3)', 'post-v3 metadata profile'],
  [webConfig, 'Guition_JC1060P470C_V3_Config', 'V3 setup AP name'],
  [sketch, 'ChipVariant=postv3', 'production silicon build'],
]) {
  requireMarker(source, marker, label);
}
if (v3DeviceHeader.includes('guition_jc1060p470c_v2') ||
    deviceDriver.includes('guition_jc1060p470c_v2') ||
    v2Sources.some(source => source.includes('DEVICE_GUITION_JC1060P470C_V3'))) {
  throw new Error('V3 implementation must be isolated from the V2 device folder');
}

console.log('Guition JC1060P470C V3 profile contract: PASS');
