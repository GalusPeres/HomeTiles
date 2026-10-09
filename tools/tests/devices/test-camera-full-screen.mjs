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
const v2 = read('src/devices/guition_jc8012p4a1_v2/device_guition_jc8012p4a1_v2.cpp');
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
  /finishPendingSwap\(\);\s*bytes = framebufferBytes\(\);\s*return inactiveFramebuffer\(\);/,
  'the decoder writes only a framebuffer the panel no longer scans');
assert.match(body(presenter, 'bool Presenter::submitFullFrame('),
  /syncCache\(frame, framebufferBytes\(\), true\)\) return false;\s*return swapTo\(frame\);/);
const end = body(presenter, 'void Presenter::endFullFrames(');
assert.match(end, /std::memcpy\(inactive, ui_copy_, framebufferBytes\(\)\);\s*if \(syncCache\(inactive, framebufferBytes\(\), false\)\) swapTo\(inactive\);/);
assert.match(end, /double_buffer_active_ = false;/, 'a later popup frame copies the whole UI again');

// Only the V2 so far; every other panel keeps the popup.
assert.match(dispatch, /#if defined\(DEVICE_GUITION_JC8012P4A1_V2\)\s*bool displayFullFrameInfo[\s\S]*?#else\s*bool displayFullFrameInfo\(uint16_t&, uint16_t&, uint16_t&\) \{ return false; \}/);
assert.match(v2, /g_camera_presenter\.fullFrameInfo\(g_rotation, width, height, turn_cw\)/);

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
const order = ['lv_refr_now(display);', 'lv_display_enable_invalidation(display, false);',
  'lv_image_set_src(ctx->image, nullptr);', 'camera_stream_stop();', 'Device::displayBeginFullFrames()',
  'mqttPublishCameraFullScreenOpen('];
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

// Full screen asks for two 8 KB chunks in flight (b315: one chunk held the
// stream at 9.4 Mbit/s, 50 KB frames at ~20 FPS); the popup keeps one.
assert.match(mqtt, /\\"view\\":\\"full\\",\\"rotate\\":%u,\\"fit\\":\\"contain\\",\\"window\\":2,/);
assert.doesNotMatch(body(mqtt, 'void mqttPublishCameraCommand('), /window/);

// Back from the full screen: only the screen outside the popup is drawn
// again; the popup keeps its last frame and status until its stream shows
// (b315 showed Buffering and stalled 350 ms on a whole-screen redraw).
assert.doesNotMatch(leave, /lv_obj_invalidate\(lv_screen_active\(\)\)/);
assert.match(leave, /lv_obj_invalidate_area\(lv_screen_active\(\), &area\)/);
assert.match(leave, /ctx->resuming = reopen && ctx->visible;/);
assert.doesNotMatch(enter, /lv_obj_clear_flag\(ctx->placeholder/);
assert.match(popup, /g_camera_popup->resuming \? nullptr : g_camera_popup->status/);
assert.match(body(popup, 'void camera_popup_set_status('),
  /if \(!error && g_camera_popup && g_camera_popup->resuming\) \{\s*camera_stream_set_external_status\(text, error\);\s*return;/);

console.log('Camera full screen: decoder straight into the framebuffer, UI kept and restored');
