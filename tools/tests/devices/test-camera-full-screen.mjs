// Camera full screen (#65): the Bridge sends frames already in the panel's
// own framebuffer size and orientation, and the hardware JPEG decoder writes
// them straight into the framebuffer the panel does not scan (no PPA, no
// scaling). Enlarging the 752 x 424 stream by PPA reached 30 FPS on the V2
// (b314) but looked pixelated, missed the screen edge by the PPA's 1/16
// steps and built the black cover up through LVGL in ~0.5 s. Entering keeps
// a copy of the UI and swaps in black at once; leaving swaps the copy back.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (relativePath) =>
  fs.readFileSync(path.join(repoRoot, relativePath), 'utf8').replace(/\r\n/g, '\n');

const presenter = read('src/devices/common/p4_dsi_camera_presenter.cpp');
const dispatch = read('src/devices/device.cpp');
const stream = read('src/video/camera_stream.cpp');
const popup = read('src/ui/popups/camera/camera_popup.cpp');
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');

const body = (source, signature) => {
  const start = source.indexOf(signature);
  assert.notEqual(start, -1, `${signature} is missing`);
  const end = source.indexOf('\n}\n', start);
  return source.slice(start, end + 2);
};

// The Bridge's clockwise turn is the PPA's counter-clockwise one inverted:
// present() uses 90 CCW for rotation & 2 on the quarter-turned panels.
const info = body(presenter, 'bool Presenter::fullFrameInfo(');
assert.match(info, /Portrait90Or270\) \{\s*turn_cw = \(rotation & 0x02U\) \? 270 : 90;\s*\} else \{\s*turn_cw = \(rotation & 0x02U\) \? 180 : 0;/);
assert.match(presenter, /present\([\s\S]*?\{\s*\/\/ Full-screen frames own both framebuffers until endFullFrames\(\)\.\s*if \(!ready_ \|\| full_frames_ \|\|/,
  'no PPA frame may land in a full-screen framebuffer');

const begin = body(presenter, 'bool Presenter::beginFullFrames(');
assert.ok(begin.indexOf('std::memcpy(ui_copy_, active, bytes);') < begin.indexOf('std::memset(inactive, 0, bytes);'),
  'the UI is kept before the screen turns black');
assert.match(begin, /std::memset\(inactive, 0, bytes\);\s*ok = syncCache\(inactive, bytes, false\) && swapTo\(inactive\);/,
  'black at once: cleared, written back, swapped in');
assert.match(body(presenter, 'uint16_t* Presenter::acquireFullFrame('),
  /finishPendingSwap\(\);\s*uint16_t\* framebuffer = inactiveFramebuffer\(\);/,
  'the decoder writes only a framebuffer the panel no longer scans');
assert.match(body(presenter, 'bool Presenter::submitFullFrame('),
  /syncCache\(frame, framebufferBytes\(\), true\)\) return false;\s*return swapTo\(frame\);/);
const end = body(presenter, 'void Presenter::endFullFrames(');
assert.match(end, /std::memcpy\(inactive, ui_copy_, framebufferBytes\(\)\);\s*if \(syncCache\(inactive, framebufferBytes\(\), false\)\) swapTo\(inactive\);/);
assert.match(end, /double_buffer_active_ = false;/, 'a later popup frame copies the whole UI again');

// Every P4 panel (user 09.10.: all devices); the ESP32-S3 panels keep the
// popup. The shared-presenter panels all use the same five calls; the Tab5
// (M5GFX) and the 4B (Arduino_GFX) have one framebuffer of their own.
assert.match(dispatch, /#if defined\(DEVICE_P4_IDF_DSI\) \|\| defined\(DEVICE_M5STACKS_TAB5\) \|\| defined\(DEVICE_WAVESHARE_4B\)\s*bool displayFullFrameInfo[\s\S]*?#else\s*bool displayFullFrameInfo\(uint16_t&, uint16_t&, uint16_t&\) \{ return false; \}/);
const presenterDevices = ['guition_jc8012p4a1_v2', 'guition_jc8012p4a1', 'guition_jc1060p470c',
  'guition_jc1060p470c_v2', 'guition_jc4880p443_portrait', 'waveshare_touch_lcd_4_3',
  'waveshare_touch_lcd_7', 'waveshare_touch_lcd_7b', 'waveshare_touch_lcd_8', 'waveshare_touch_lcd_10_1'];
for (const name of presenterDevices) {
  const source = read(`src/devices/${name}/device_${name}.cpp`);
  const header = read(`src/devices/${name}/device_${name}.h`);
  assert.match(source, /g_camera_presenter\.fullFrameInfo\(g_rotation, width, height, turn_cw\)/, name);
  assert.match(source, /return g_panel_fb_ready && g_camera_presenter\.beginFullFrames\(\);/, name);
  assert.match(source, /return g_panel_fb_ready \? g_camera_presenter\.acquireFullFrame\(bytes\) : nullptr;/, name);
  assert.match(source, /return g_panel_fb_ready && g_camera_presenter\.submitFullFrame\(\);/, name);
  assert.match(source, /if \(g_panel_fb_ready\) g_camera_presenter\.endFullFrames\(\);/, name);
  assert.match(header, /bool displayFullFrameInfo\(uint16_t& width, uint16_t& height, uint16_t& turn_cw\);\s*bool displayBeginFullFrames\(\);/, name);
}
const deviceSelect = read('src/devices/device_select.h');
const dsiEnd = deviceSelect.indexOf('#define DEVICE_P4_IDF_DSI');
const dsiBlock = deviceSelect.slice(deviceSelect.lastIndexOf('#if ', dsiEnd), dsiEnd);
for (const name of presenterDevices) {
  assert.ok(dsiBlock.includes(`defined(DEVICE_${name.toUpperCase()})`), `${name} is a DEVICE_P4_IDF_DSI panel`);
}

// Frames are multiples of 16 high (JPEG blocks; the Bridge checks it): a
// 1024 x 600 panel gets 592 rows, centred on a cache line, the rest black.
const rows = body(presenter, 'bool Presenter::fullFrameRows(');
assert.match(rows, /config_\.panel_width % 16 != 0\) return false;/);
assert.match(rows, /rows = config_\.panel_height - config_\.panel_height % 16;/);
assert.match(rows, /% kCacheLineSize != 0\) --top;/);
const acquire = body(presenter, 'uint16_t* Presenter::acquireFullFrame(');
assert.match(acquire, /std::memset\(framebuffer, 0,/);
assert.match(acquire, /bytes = static_cast<size_t>\(rows\) \* row_pixels \* sizeof\(uint16_t\);\s*return framebuffer \+ static_cast<size_t>\(top\) \* row_pixels;/);

// The Tab5 (M5GFX, one framebuffer): the decoder writes the scanned
// framebuffer right after a refresh, faster than the panel reads it.
const tab5 = read('src/devices/m5stacks_tab5/device_m5stacks_tab5.cpp');
assert.match(tab5, /panel->\*\(&PanelDsiAccess::_disp_panel_handle\)/);
assert.match(tab5, /cbs\.on_refresh_done = on_full_refresh_done;/);
assert.match(body(tab5, 'uint16_t* DeviceM5StacksTab5::displayAcquireFullFrame('),
  /xSemaphoreTake\(g_full_refresh, 0\);\s*xSemaphoreTake\(g_full_refresh, pdMS_TO_TICKS\(40\)\);/);
const tab5Begin = body(tab5, 'bool DeviceM5StacksTab5::displayBeginFullFrames(');
assert.ok(tab5Begin.indexOf('std::memcpy(g_full_ui_copy, g_panel_fb') < tab5Begin.indexOf('std::memset(g_panel_fb, 0'));
assert.match(body(tab5, 'void DeviceM5StacksTab5::displayEndFullFrames('), /std::memcpy\(g_panel_fb, g_full_ui_copy, kPanelFrameBytes\);\s*sync_panel_fb\(false\);/);
const b4 = read('src/devices/waveshare_4b/device_waveshare_4b.cpp');
assert.match(body(b4, 'bool DeviceWaveshare4B::displayFullFrameInfo('),
  /\(g_gfx->getRotation\(\) & 0x01\) != 0[\s\S]*?turn_cw = g_gfx->getRotation\(\) == 2 \? 180 : 0;/,
  'the 4B: only rotations 0 and 180, like its direct popup frames');
const b4Begin = body(b4, 'bool DeviceWaveshare4B::displayBeginFullFrames(');
assert.ok(b4Begin.indexOf('memcpy(g_full_ui_copy, framebuffer, bytes);') < b4Begin.indexOf('memset(framebuffer, 0, bytes);'));
assert.match(body(b4, 'bool DeviceWaveshare4B::displaySubmitFullFrame('), /ESP_CACHE_MSYNC_FLAG_DIR_M2C \| ESP_CACHE_MSYNC_FLAG_INVALIDATE/);
assert.match(body(tab5, 'bool DeviceM5StacksTab5::displayFullFrameInfo('),
  /turn_cw = \(g_rotation & 0x02\) \? 180 : 0;[\s\S]*?turn_cw = \(g_rotation & 0x02\) \? 270 : 90;/);

// The worker keeps the newest JPEG; the UI loop decodes it in the
// framebuffer's own byte order and checks the size first.
assert.match(stream, /full \? queue_full_frame\(input, jpeg_bytes\)\s*: decode_jpeg_frame\(input, jpeg_bytes\)/);
const process = body(stream, 'static void process_full_frame(');
assert.match(process, /Device::displayAcquireFullFrame\(frame_bytes\)/);
assert.match(process, /info\.width != g_full_width \|\| info\.height != g_full_height/);
assert.match(process, /decode_config\.rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;/);
assert.match(process, /Dma2dArbiterGuard dma2d_guard\(100\);/);
assert.match(process, /shown = Device::displaySubmitFullFrame\(\);/);
assert.match(stream, /if \(g_full_shown\) \{\s*process_full_frame\(\);\s*return;\s*\}/);
assert.match(body(stream, 'void camera_stream_stop('), /g_stop_requested = true;[\s\S]*?g_full_shown = false;/,
  'after stop the UI never touches the slots the worker frees');

// The popup: both switches run in the loop, LVGL draws nothing meanwhile.
const enter = body(popup, 'static void enter_full_screen(');
const order = ['lv_image_set_src(ctx->image, nullptr);', 'lv_obj_add_flag(ctx->image, LV_OBJ_FLAG_HIDDEN);',
  'lv_obj_invalidate(lv_obj_get_parent(ctx->image));', 'lv_refr_now(display);',
  'lv_display_enable_invalidation(display, false);', 'Device::displayBeginFullFrames()',
  'camera_stream_stop();', 'mqttPublishCameraFullScreenOpen('];
for (let i = 1; i < order.length; ++i) {
  assert.ok(enter.indexOf(order[i - 1]) < enter.indexOf(order[i]), `${order[i - 1]} before ${order[i]}`);
}
const leave = body(popup, 'static void leave_full_screen(');
assert.ok(leave.indexOf('Device::displayEndFullFrames();') < leave.indexOf('lv_display_enable_invalidation(display, true);'));
assert.match(popup, /static void video_event_cb[\s\S]*?full_request = kFullEnter;/);
assert.match(popup, /if \(full_request == kFullEnter\) \{\s*enter_full_screen\(\);\s*\} else if \(full_request == kFullLeave\) \{\s*leave_full_screen\(true\);/);
assert.match(popup, /if \(g_camera_popup->full\) \{\s*\/\/ A Bridge before full-screen frames[\s\S]*?leave_full_screen\(true\);/,
  'an older Bridge refuses: back to the popup');
assert.match(body(popup, 'static void start_camera_stream('), /if \(camera_stream_is_active\(\)\) \{\s*ctx->pending_url = url;/,
  'a new stream waits for the previous task (stop is asynchronous)');
assert.match(body(popup, 'static void close_camera_popup('), /leave_full_screen\(false\);/);

// Every stream asks for the transport camera_transport picks: 32 KB chunks
// with two in flight (b319: the panel receives 28 Mbit/s, one 8 KB chunk at a
// time gave 12), the 8 KB one-at-a-time protocol as the fallback.
const transport = read('src/video/camera_transport.cpp');
assert.match(transport, /constexpr uint32_t kFastChunkBytes = 32 \* 1024;\s*constexpr uint8_t kFastWindow = 2;\s*constexpr uint32_t kSafeChunkBytes = 8 \* 1024;\s*constexpr uint8_t kSafeWindow = 1;/);
assert.match(transport, /if \(prefs\.getBool\(kRunKey, false\)\) \{\s*prefs\.putBool\(kRunKey, false\);\s*prefs\.putString\(kSafeKey, FW_VERSION\);\s*g_safe = true;/,
  'a restart while a fast stream ran: 8 KB for this firmware');
assert.match(transport, /prefs\.getString\(kSafeKey, ""\) == FW_VERSION/, 'a new firmware tries the fast transport again');
for (const fn of ['void mqttPublishCameraFullScreenOpen(', 'void mqttPublishCameraCommand(']) {
  assert.match(body(mqtt, fn), /\\"chunk\\":%u,\\"window\\":%u%s,/);
  assert.match(body(mqtt, fn), /camera_quality_field\(quality, sizeof\(quality\)\);/);
  assert.match(body(mqtt, fn), /static_cast<unsigned>\(transport\.chunk_bytes\),\s*static_cast<unsigned>\(transport\.window\), quality\);/);
}
// The panel's JPEG quality only where it asks for one (S3 b329): the P4
// keeps the Bridge's 11, older Bridges ignore the field.
assert.match(body(mqtt, 'static void camera_quality_field('), /if \(camera_geometry::kJpegQuality\) \{\s*snprintf\(out, size, ",\\"quality\\":%u",/);
const geometry = read('src/video/camera_geometry.h');
assert.match(geometry, /#if defined\(DEVICE_ESP32_S3_RGB_480\)[\s\S]*?kJpegQuality = 5;[\s\S]*?#else[\s\S]*?kJpegQuality = 0;\s*#endif/);
// A restart the user or an update asked for clears the "fast stream runs"
// mark (b327: an OTA during a stream fell back for the new firmware); the
// recovery restarts (network wedge, display timeout) keep it.
assert.match(transport, /void planned_restart\(\) \{\s*if \(g_run_marked\) write_run_mark\(false\);\s*\}/);
const handlerUtils = read('src/web/server/handlers/web_admin_handler_utils.h');
assert.match(body(handlerUtils, 'inline void prepareDisplayForRestart('), /camera_transport::planned_restart\(\);/);
const ino = read('HomeTiles.ino');
assert.match(ino, /\[Update\] Successful - restarting"\);\s*camera_transport::planned_restart\(\);/);
assert.match(body(ino, 'static void apply_system_reboot('), /camera_transport::planned_restart\(\);/);
for (const recovery of ['src/network/network_manager.cpp', 'src/devices/common/p4_dsi_camera_presenter.cpp']) {
  assert.doesNotMatch(read(recovery), /planned_restart/, `${recovery}: a recovery restart keeps the fallback`);
}
assert.match(stream, /constexpr size_t kCameraMaxChunkBytes = 32 \* 1024;/);
assert.match(body(stream, 'static void run_camera_task('), /chunk_bytes > kCameraMaxChunkBytes\) \{/);
assert.equal((body(stream, 'static void run_camera_task(').match(/g_transport_failed = !g_stop_requested;/g) || []).length, 4,
  'safety stop, receive, block and acknowledgement errors count as transport failures');
assert.match(popup, /if \(camera_stream_take_transport_failure\(\) && g_camera_popup->stream_fast\) \{\s*g_camera_popup->stream_fast = false;\s*camera_transport::fall_back\(/,
  'a fast stream ending in a transport error: 8 KB until restart, the same view again');
assert.match(body(popup, 'static void close_camera_popup('), /camera_transport::popup_closed\(\);/);

// Black instead of a still frame both ways (user, b320): the screen black
// until the first full frame (b318's enlarged popup frame was smaller and
// stood still), and the popup kept with its video area black, so it comes
// back black until its stream runs (not with its last frame).
assert.doesNotMatch(presenter, /drawPreview/);
assert.doesNotMatch(stream, /camera_stream_shown_frame/);
assert.match(enter, /lv_obj_add_flag\(ctx->placeholder, LV_OBJ_FLAG_HIDDEN\);/);
assert.match(presenter, /void Presenter::end\(\) \{\s*\/\/ A stream stopping under the full screen leaves it to endFullFrames\(\)\.\s*if \(full_frames_ \|\| !double_buffer_active_\) return;/);

// Back from the full screen: only the screen outside the popup is drawn
// again; the popup keeps its black video and status until its stream shows
// (b315 showed Buffering and stalled 350 ms on a whole-screen redraw).
assert.doesNotMatch(leave, /lv_obj_invalidate\(lv_screen_active\(\)\)/);
assert.match(leave, /lv_obj_invalidate_area\(lv_screen_active\(\), &area\)/);
assert.match(leave, /ctx->resuming = reopen && ctx->visible;/);
assert.doesNotMatch(enter, /lv_obj_clear_flag\(ctx->placeholder/);
assert.match(popup, /g_camera_popup->resuming \? nullptr : g_camera_popup->status/);
assert.match(body(popup, 'void camera_popup_set_status('),
  /if \(!error && g_camera_popup && g_camera_popup->resuming\) \{\s*camera_stream_set_external_status\(text, error\);\s*return;/);

console.log('Camera full screen: decoder straight into the framebuffer, UI kept and restored');
