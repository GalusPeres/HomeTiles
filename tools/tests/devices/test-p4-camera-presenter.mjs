import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), 'utf8');
}

function requireMarker(source, marker, label) {
  assert.ok(source.includes(marker), `${label} is missing: ${marker}`);
}

function functionBody(source, qualifiedName) {
  const start = source.indexOf(`bool ${qualifiedName}(`);
  assert.notEqual(start, -1, `${qualifiedName} is missing`);
  const end = source.indexOf('\nvoid ', start);
  assert.notEqual(end, -1, `${qualifiedName} end marker is missing`);
  return source.slice(start, end);
}

const presenterHeader = read('src/devices/common/p4_dsi_camera_presenter.h');
const presenterSource = read('src/devices/common/p4_dsi_camera_presenter.cpp');
const arbiterHeader = read('src/core/display/dma2d_arbiter.h');
const crashLog = read('src/core/diagnostics/crash_log.cpp');
const cameraStream = read('src/video/camera_stream.cpp');
const deviceSelect = read('src/devices/device_select.h');
const deviceDispatch = read('src/devices/device.cpp');

assert.ok(
  presenterHeader.indexOf('#include <sdkconfig.h>') <
    presenterHeader.indexOf('#if defined(CONFIG_IDF_TARGET_ESP32P4)'),
  'shared presenter must load the ESP-IDF target before its ESP32-P4 guard',
);

for (const marker of [
  'uint16_t* framebuffers_[2]',
  'uint16_t* activeFramebuffer() const',
  'bool syncUiToInactive()',
  'Transform::Portrait90Or270',
  'Native0Or180,',
]) {
  requireMarker(presenterHeader + presenterSource, marker, 'shared presenter');
}

requireMarker(arbiterHeader, 'bool detach()', 'DMA2D quarantine API');
requireMarker(presenterSource, 'Dma2dArbiterGuard dma2d_guard', 'presenter arbitration');
requireMarker(presenterSource, 'dma2d_guard.detach();', 'timeout quarantine');
requireMarker(
  presenterSource,
  'restartAfterTimeout(x, y, w, h, source_stride, rotation);',
  'fail-closed timeout handoff',
);
requireMarker(
  presenterSource,
  'ESP_CACHE_MSYNC_FLAG_DIR_M2C',
  'PPA output cache invalidation',
);
requireMarker(
  presenterSource,
  'esp_lcd_panel_draw_bitmap(',
  'DSI framebuffer swap',
);
requireMarker(presenterSource, 'bool Presenter::flushFramebufferRect', 'checked cache flush');
requireMarker(
  presenterSource,
  'if (!flushFramebufferRect(inactive, x, y, w, h)) return false;',
  'dirty mirror cache failure propagation',
);
requireMarker(presenterSource, 'faultCooldownActive()', 'shared presenter fault cooldown');
requireMarker(crashLog, '=== P4 display pipeline timeout', 'persistent timeout report');
requireMarker(presenterSource, 'if (!waitRefreshDone())', 'confirmed DSI swap');
requireMarker(
  presenterSource,
  'restartAfterDisplayTimeout(',
  'fail-closed DSI refresh timeout',
);
const swapIndex = presenterSource.indexOf('const esp_err_t swap_err');
const refreshDrainIndex = presenterSource.indexOf('drainRefreshSignal();', swapIndex);
const refreshWaitIndex = presenterSource.indexOf('if (!waitRefreshDone())', swapIndex);
assert.ok(
  swapIndex >= 0 && refreshDrainIndex > swapIndex && refreshWaitIndex > refreshDrainIndex,
  'continuous VSYNC semaphore must be drained after the accepted draw and before waiting',
);
requireMarker(deviceSelect, '#define DEVICE_P4_IDF_DSI', 'shared DSI profile group');
assert.match(
  deviceDispatch,
  /void displayEndFullFramePreview\(\) \{\s*#if defined\(DEVICE_P4_IDF_DSI\)\s*DeviceImpl::displayEndFullFramePreview\(\);/,
  'all native P4 DSI profiles must end the shared preview lifecycle',
);

const profiles = [
  {
    file: 'src/devices/waveshare_touch_lcd_4_3/device_waveshare_touch_lcd_4_3.cpp',
    header: 'src/devices/waveshare_touch_lcd_4_3/device_waveshare_touch_lcd_4_3.h',
    qualified: 'DeviceWaveshareTouchLCD4_3::displayTryFullFramePreview',
    transform: 'Transform::Portrait90Or270',
    panel: [480, 800],
    logical: [800, 480],
  },
  {
    file: 'src/devices/waveshare_touch_lcd_7/device_waveshare_touch_lcd_7.cpp',
    header: 'src/devices/waveshare_touch_lcd_7/device_waveshare_touch_lcd_7.h',
    qualified: 'DeviceWaveshareTouchLCD7::displayTryFullFramePreview',
    transform: 'Transform::Portrait90Or270',
    panel: [720, 1280],
    logical: [1280, 720],
  },
  {
    file: 'src/devices/waveshare_touch_lcd_7b/device_waveshare_touch_lcd_7b.cpp',
    header: 'src/devices/waveshare_touch_lcd_7b/device_waveshare_touch_lcd_7b.h',
    qualified: 'DeviceWaveshareTouchLCD7B::displayTryFullFramePreview',
    transform: 'Transform::Native0Or180',
    panel: [1024, 600],
    logical: [1024, 600],
  },
  {
    file: 'src/devices/waveshare_touch_lcd_8/device_waveshare_touch_lcd_8.cpp',
    header: 'src/devices/waveshare_touch_lcd_8/device_waveshare_touch_lcd_8.h',
    qualified: 'DeviceWaveshareTouchLCD8::displayTryFullFramePreview',
    transform: 'Transform::Portrait90Or270',
    panel: [800, 1280],
    logical: [1280, 800],
  },
  {
    file: 'src/devices/waveshare_touch_lcd_10_1/device_waveshare_touch_lcd_10_1.cpp',
    header: 'src/devices/waveshare_touch_lcd_10_1/device_waveshare_touch_lcd_10_1.h',
    qualified: 'DeviceWaveshareTouchLCD10::displayTryFullFramePreview',
    transform: 'Transform::Portrait90Or270',
    panel: [800, 1280],
    logical: [1280, 800],
  },
  {
    file: 'src/devices/guition_jc8012p4a1/device_guition_jc8012p4a1.cpp',
    header: 'src/devices/guition_jc8012p4a1/device_guition_jc8012p4a1.h',
    qualified: 'DeviceGuitionJC8012P4A1::displayTryFullFramePreview',
    transform: 'Transform::Portrait90Or270',
    panel: [800, 1280],
    logical: [1280, 800],
    genericPpa: false,
  },
  {
    file: 'src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.cpp',
    header: 'src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.h',
    qualified: 'DeviceGuitionJC8012P4A1V2::displayTryFullFramePreview',
    transform: 'Transform::Portrait90Or270',
    panel: [800, 1280],
    logical: [1280, 800],
    genericPpa: false,
  },
  {
    file: 'src/devices/guition_jc1060p470c/device_guition_jc1060p470c.cpp',
    header: 'src/devices/guition_jc1060p470c/device_guition_jc1060p470c.h',
    qualified: 'DeviceGuitionJC1060P470C::displayTryFullFramePreview',
    transform: 'Transform::Native0Or180',
    panel: [1024, 600],
    logical: [1024, 600],
  },
  {
    file: 'src/devices/guition_jc1060p470c_v2/device_guition_jc1060p470c_v2.cpp',
    header: 'src/devices/guition_jc1060p470c_v2/device_guition_jc1060p470c_v2.h',
    qualified: 'DeviceGuitionJC1060P470CV2::displayTryFullFramePreview',
    transform: 'Transform::Native0Or180',
    panel: [1024, 600],
    logical: [1024, 600],
  },
  {
    file: 'src/devices/guition_jc4880p443_portrait/device_guition_jc4880p443_portrait.cpp',
    header: 'src/devices/guition_jc4880p443_portrait/device_guition_jc4880p443_portrait.h',
    qualified: 'DeviceGuitionJC4880P443Portrait::displayTryFullFramePreview',
    transform: 'Transform::Native0Or180',
    panel: [480, 800],
    logical: [480, 800],
    genericPpa: false,
  },
];

for (const profile of profiles) {
  const source = read(profile.file);
  const header = read(profile.header);
  requireMarker(source, 'kPanelFrameBufferCount = 2', `${profile.file} double buffer`);
  assert.match(
    source,
    /esp_lcd_dpi_panel_get_frame_buffer\([\s\S]*?&fb0, &fb1\)/,
    `${profile.file} must request both driver-owned framebuffers`,
  );
  requireMarker(source, profile.transform, `${profile.file} transform`);
  requireMarker(source, 'g_camera_presenter.noteUiWrite', `${profile.file} UI mirroring`);
  if (profile.genericPpa !== false) {
    requireMarker(source, 'restartAfterPpaTimeout(', `${profile.file} generic PPA fail-close`);
  }
  requireMarker(header, 'void displayEndFullFramePreview();', `${profile.header} lifecycle`);

  const preview = functionBody(source, profile.qualified);
  requireMarker(preview, 'g_camera_presenter.present(', `${profile.file} preview delegation`);
  assert.ok(!preview.includes('ppa_do_scale_rotate_mirror'), `${profile.file} duplicates PPA submit`);
  assert.ok(!preview.includes('preview_disabled_after_fault'), `${profile.file} permanently disables preview`);
  assert.ok(!preview.includes('kPpaMinRotateWidth'), `${profile.file} rejects 1024x600 camera geometry`);

  const [panelWidth, panelHeight] = profile.panel;
  const [logicalWidth, logicalHeight] = profile.logical;
  if (profile.transform.endsWith('Portrait90Or270')) {
    assert.deepEqual([panelWidth, panelHeight], [logicalHeight, logicalWidth]);
  } else {
    assert.deepEqual([panelWidth, panelHeight], [logicalWidth, logicalHeight]);
  }
}

const tab5 = read('src/devices/m5stacks_tab5/device_m5stacks_tab5.cpp');
requireMarker(tab5, 'dma2d_guard.detach();', 'Tab5 timeout quarantine');
requireMarker(tab5, 'restartAfterPpaTimeout(', 'Tab5 diagnosable fail-close');
requireMarker(tab5, 'PPA async callback unavailable', 'Tab5 safe callback fallback');
assert.ok(
  !tab5.includes('PPA_TRANS_MODE_BLOCKING'),
  'Tab5 must not enter an unbounded blocking PPA operation',
);

const b4 = read('src/devices/waveshare_4b/device_waveshare_4b.cpp');
const b4Preview = functionBody(b4, 'DeviceWaveshare4B::displayTryFullFramePreview');
assert.ok(!b4Preview.includes('ppa_do_scale_rotate_mirror'), 'B4 must remain on its synchronous GFX path');

const cameraTask = cameraStream.slice(
  cameraStream.indexOf('static void camera_task(void*)'),
  cameraStream.indexOf('\n}  // namespace', cameraStream.indexOf('static void camera_task(void*)')),
);
requireMarker(cameraTask, 'if (!release_buffers) g_task = nullptr;', 'no-cleanup task handoff');
const releaseIndex = cameraTask.indexOf('release_frame_buffers();');
const finalNullIndex = cameraTask.indexOf('g_task = nullptr;', releaseIndex);
assert.ok(releaseIndex >= 0 && finalNullIndex > releaseIndex, 'task must stay active until buffer release completes');

console.log('P4 camera presenter contract: PASS');
