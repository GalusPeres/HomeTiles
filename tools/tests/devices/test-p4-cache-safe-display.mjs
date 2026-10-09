// P4 panels flashed blue during flash writes: the DSI DMA interrupt that
// re-arms every frame was masked while the cache was off. The rebuilt
// cache-safe IDF objects must ship for both P4 variants, be injected by the
// local build and CI, and every interrupt callback they call must be in IRAM.
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';

import {readRepoFile, repoRoot} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const fixDir = 'tools/esp-idf-3.3.7-p4-cache-safe';
const readme = read(`${fixDir}/README.md`);
const objects = {
  'esp_lcd_panel_dpi.c.obj': ['libesp_lcd.a', 'on_refresh_done callback not in IRAM'],
  'dw_gdma.c.obj': ['libesp_hw_support.a', 'on_full_trans_done not in IRAM'],
  'esp_cam_ctlr_csi.c.obj': ['libesp_driver_cam.a', 'on_get_new_trans callback not in IRAM'],
};

for (const variant of ['esp32p4-libs', 'esp32p4_es-libs']) {
  for (const [name, [, marker]] of Object.entries(objects)) {
    const file = path.join(repoRoot, fixDir, variant, name);
    assert.ok(fs.existsSync(file), `${variant}/${name} is missing`);
    const bytes = fs.readFileSync(file);
    const sha = crypto.createHash('sha256').update(bytes).digest('hex');
    assert.ok(readme.includes(`| ${variant} | ${name} | \`${sha}\` |`), `${variant}/${name} hash is not documented`);
    assert.ok(bytes.includes(Buffer.from(marker)), `${variant}/${name} is not the cache-safe build`);
  }
}

const additions = read(`${fixDir}/sdkconfig-additions.h`);
for (const option of ['LCD_DSI_ISR_CACHE_SAFE', 'DW_GDMA_ISR_IRAM_SAFE', 'DW_GDMA_OBJ_DRAM_SAFE',
  'CAM_CTLR_MIPI_CSI_ISR_CACHE_SAFE']) {
  assert.match(additions, new RegExp(`#define CONFIG_${option} 1`), option);
}
assert.match(read(`${fixDir}/dw_gdma-config-transfer-iram.patch`),
  /\+IRAM_ATTR esp_err_t dw_gdma_channel_config_transfer\(/);

// Local builds and CI inject every object into its archive and verify it.
const apply = read('tools/apply-p4-cache-safe-local.ps1');
for (const [name, [archive]] of Object.entries(objects)) {
  assert.match(apply, new RegExp(`'${name.replace(/\./g, '\\.')}'\\s*=\\s*'${archive.replace(/\./g, '\\.')}'`), name);
}
assert.match(apply, /foreach \(\$variant in @\('esp32p4-libs', 'esp32p4_es-libs'\)\)/);
const build = read('tools/build-firmware-local.ps1');
assert.match(build, /if \(-not \$isNativeS3\) \{\n    & \(Join-Path \$PSScriptRoot 'apply-esp-hosted-3\.3\.7-fixes-local\.ps1'\)[\s\S]*?& \(Join-Path \$PSScriptRoot 'apply-p4-cache-safe-local\.ps1'\)\n\}/);
assert.match(build, /Select-String -SimpleMatch 'on_full_trans_done not in IRAM'/);
const workflow = read('.github/workflows/firmware.yml');
assert.match(workflow, /- name: Apply cache-safe P4 display and camera DMA objects\n        if: matrix\.rx_variant != 'native-s3' && !inputs\.repair_s3_release/);
assert.match(workflow, /for pair in esp_lcd_panel_dpi\.c\.obj:libesp_lcd\.a dw_gdma\.c\.obj:libesp_hw_support\.a esp_cam_ctlr_csi\.c\.obj:libesp_driver_cam\.a; do/);
assert.match(workflow, /grep -a -F -q "on_full_trans_done not in IRAM" "\$\{firmware_bin\}"/);

// With the cache off these callbacks run from IRAM: every P4 board's DPI
// callbacks and the CSI callbacks of the built-in camera.
const devicesDir = path.join(repoRoot, 'src/devices');
let boards = 0;
for (const dir of fs.readdirSync(devicesDir)) {
  const folder = path.join(devicesDir, dir);
  if (!fs.statSync(folder).isDirectory()) continue;
  for (const file of fs.readdirSync(folder).filter(name => name.endsWith('.cpp'))) {
    const source = fs.readFileSync(path.join(folder, file), 'utf8');
    if (!source.includes('esp_lcd_dpi_panel_register_event_callbacks(')) continue;
    ++boards;
    // Every DPI callback the file registers (the Tab5 only on_refresh_done for
    // its camera full screen, on M5GFX's panel).
    const callbacks = [...source.matchAll(/\bcbs\.on_(?:color_trans_done|refresh_done) = (\w+);/g)].map(m => m[1]);
    assert.ok(callbacks.length > 0, `${dir}/${file} registers no named DPI callback`);
    for (const callback of callbacks) {
      assert.match(source, new RegExp(`bool IRAM_ATTR ${callback}\\(`), `${dir}/${file} ${callback}`);
    }
  }
}
assert.ok(boards >= 10, `expected the shared P4 DSI boards, found ${boards}`);
const camera = read('src/video/local_camera/local_camera.cpp');
assert.match(camera, /IRAM_ATTR bool onGetNewTransaction\(/);
assert.match(camera, /IRAM_ATTR bool onTransactionFinished\(/);

console.log(`P4 cache-safe display/camera: objects, local + CI injection, ${boards} board drivers and camera callbacks pass.`);
