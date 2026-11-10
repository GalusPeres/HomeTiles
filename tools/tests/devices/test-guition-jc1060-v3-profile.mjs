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
const profile = catalog.profiles.find((item) => item.key === 'guition_jc1060p470_v3');
if (!profile ||
    profile.buildProfile !== 'guition_jc1060p470_v3' ||
    profile.define !== 'DEVICE_GUITION_JC1060P470_V3' ||
    profile.metadataDeviceKey !== 'guition_jc1060p470_v3' ||
    profile.chipFamily !== 'ESP32-P4' ||
    profile.flashSize !== 16 * 1024 * 1024 ||
    profile.siliconVariant !== 'post_v3' ||
    profile.minimumRevision !== 301 ||
    profile.maximumRevision !== 399 ||
    profile.installer?.status !== 'validation-pending' ||
    profile.installer?.acceptsLegacyDescriptor !== false) {
  throw new Error('JC1060P470 V3 release profile is incomplete or unsafe');
}

const deviceSelect = read('src/devices/device_select.h');
const activeDevice = read('src/devices/active_device.h');
const v2DeviceHeader = read(
  'src/devices/guition_jc1060p470c_v2/device_guition_jc1060p470c_v2.h');
const v3DeviceHeader = read(
  'src/devices/guition_jc1060p470_v3/device_guition_jc1060p470_v3.h');
const displayConfig = read(
  'src/devices/guition_jc1060p470c_v2/vendor/displays_config.h');
const panelDriver = read(
  'src/devices/guition_jc1060p470c_v2/vendor/esp_lcd_jd9165.c');
const deviceDriver = read(
  'src/devices/guition_jc1060p470c_v2/device_guition_jc1060p470c_v2.cpp');
const metadata = read('src/core/firmware/firmware_metadata.cpp');
const sketch = read('sketch.yaml');

for (const [source, marker, label] of [
  [deviceSelect, 'defined(DEVICE_GUITION_JC1060P470_V3)', 'device selection'],
  [deviceSelect, 'defined(DEVICE_GUITION_JC1060P470_V3) ||', 'P4 DSI registration'],
  [activeDevice, 'namespace DeviceImpl = DeviceGuitionJC1060P470V3;', 'V3 device interface'],
  [v3DeviceHeader, 'using namespace DeviceGuitionJC1060P470CV2;', 'shared panel lifecycle'],
  [v3DeviceHeader, '"guition_jc1060p470_v3"', 'V3 runtime identity'],
  [v3DeviceHeader, 'Device::kNoHardwareIoProfile', 'unverified expansion GPIOs'],
  [v2DeviceHeader, '"guition_jc1060p470c_v2"', 'unchanged V2 runtime identity'],
  [displayConfig, '52000000', 'V2 pixel clock'],
  [displayConfig, '750', 'V2 DSI lane rate'],
  [panelDriver, 'defined(DEVICE_GUITION_JC1060P470_V3)', 'V2 JD9165 init reuse'],
  [deviceDriver, 'bus_cfg.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT;', 'production-silicon DSI clock'],
  [deviceDriver, 'bus_cfg.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M;', 'pre-v3 DSI clock'],
  [metadata, 'FW_META_TARGET_DEVICE_KEY "guition_jc1060p470_v3"', 'V3 OTA identity'],
  [metadata, 'defined(DEVICE_GUITION_JC1060P470_V3)', 'post-v3 metadata profile'],
  [sketch, 'ChipVariant=postv3', 'production silicon build'],
]) {
  requireMarker(source, marker, label);
}
if (v2DeviceHeader.includes('"guition_jc1060p470_v3"')) {
  throw new Error('V3 identity must stay in the V3 device profile');
}

console.log('Guition JC1060P470 V3 profile contract: PASS');
