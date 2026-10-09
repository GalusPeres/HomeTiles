// The ESP32-S3 480 x 480 panels show the camera too (user 2026-10-09): the
// same TCP stream as the P4, decoded in software (tjpgd) in the camera task
// and drawn by LVGL; the full screen shows the whole picture (480 x 480 with
// the Bridge's black bars, nothing cut) at fewer frames per second.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (relativePath) =>
  fs.readFileSync(path.join(repoRoot, relativePath), 'utf8').replace(/\r\n/g, '\n');

const stream = read('src/video/camera_stream.cpp');
const geometry = read('src/video/camera_geometry.h');
const popup = read('src/ui/popups/camera/camera_popup.cpp');
const registry = read('src/types/types_registry.cpp');

// One stream module: the P4 hardware decoder, or tjpgd on the S3.
assert.match(stream, /SOC_JPEG_DECODE_SUPPORTED\n#define CAMERA_STREAM_HW_JPEG 1\n#elif defined\(DEVICE_ESP32_S3_RGB_480\)\n#define CAMERA_STREAM_HW_JPEG 0/);
assert.match(stream, /#include <libs\/tjpgd\/tjpgd\.h>/);
assert.match(stream, /JRESULT result = jd_prepare\(&decoder, soft_jpeg_input, g_soft_work, kSoftWorkBytes, &input\);/);
// The popup's frames exactly its size; the full screen's the whole picture
// without bars (Bridge fit "inside", b330: the bars were almost half of each
// 480 x 480 frame), any size within the screen, its own size for LVGL.
assert.match(stream, /const bool size_ok = g_full_mode\s*\? decoder\.width <= g_frame_w && decoder\.height <= g_frame_h\s*: decoder\.width == g_frame_w && decoder\.height == g_frame_h;/);
assert.match(stream, /g_images\[index\]\.header\.w = decoder\.width;\s*g_images\[index\]\.header\.h = decoder\.height;\s*publish_write_buffer\(index\);/);
assert.match(stream, /g_images\[i\]\.header\.cf = LV_COLOR_FORMAT_RGB565;/, 'native RGB565: LVGL copies the frames unconverted');
assert.match(stream, /const bool full = CAMERA_STREAM_HW_JPEG && g_full_mode;/, 'software decoding decodes the full screen\'s frames in the task too');
assert.match(stream, /lv_image_cache_drop\(&g_images\[ready\]\);\s*lv_image_header_cache_drop\(&g_images\[ready\]\);\s*lv_image_set_src\(image, &g_images\[ready\]\);\s*lv_obj_invalidate\(image\);/,
  'a reused buffer with new pixels or a new size is drawn again');
assert.match(stream, /g_frame_w = full_screen \? camera_geometry::kSoftFullWidth : kWidth;/);
// b327 left the S3's full screen black: the hardware full-screen flag skipped
// LVGL's presentation; and the other view's last frame showed while switching.
assert.match(stream, /g_full_shown = CAMERA_STREAM_HW_JPEG && full_screen;/);
assert.match(stream, /#if !CAMERA_STREAM_HW_JPEG\n[^#]*take_frame = take_frame && !g_stop_requested;\n#endif\n  if \(take_frame\) \{/);

// The S3's sizes and rate: the whole screen in full screen, a few frames.
assert.match(geometry, /#if defined\(DEVICE_ESP32_S3_RGB_480\)[\s\S]*?kFps = 8;[\s\S]*?kSoftFullWidth = 480;\s*inline constexpr uint16_t kSoftFullHeight = 480;/);
// b329's JPEG quality 5 looked no different and cost FPS: the Bridge's own.
assert.match(geometry, /#if defined\(DEVICE_ESP32_S3_RGB_480\)[\s\S]*?kJpegQuality = 0;[\s\S]*?kFullFit = "inside";[\s\S]*?#else[\s\S]*?kFullFit = "contain";/);
const mqttHandlers = read('src/network/mqtt/mqtt_handlers.cpp');
assert.match(mqttHandlers, /\\"fit\\":\\"%s\\",[\s\S]{0,400}?camera_geometry::kFullFit,/);
assert.match(popup, /lv_obj_set_size\(ctx->soft_full_image, LV_SIZE_CONTENT, LV_SIZE_CONTENT\);\s*lv_obj_center\(ctx->soft_full_image\);/,
  'the frame centred on black at its own size');

// The popup: the camera tile is no longer left out, the S3 keeps its
// internal draw band, its full screen is LVGL on black.
assert.doesNotMatch(registry, /#if !defined\(DEVICE_ESP32_S3_RGB_480\)\s*\{\s*TILE_CAMERA/);
assert.match(popup, /constexpr bool kLargeDrawBuffer = false;/);
const softEnter = popup.slice(popup.indexOf('static void enter_soft_full_screen('), popup.indexOf('static void leave_soft_full_screen('));
assert.ok(softEnter.indexOf('lv_image_set_src(ctx->image, nullptr);') < softEnter.indexOf('camera_stream_stop();'),
  'LVGL lets go of the popup frame before the stream frees it');
assert.match(softEnter, /ctx->full_width = camera_geometry::kSoftFullWidth;\s*ctx->full_height = camera_geometry::kSoftFullHeight;/,
  'the 480 x 480 answer of the Bridge is checked against the full screen (b325 refused it)');
assert.match(softEnter, /mqttPublishCameraFullScreenOpen\(ctx->entity_id\.c_str\(\), camera_geometry::kFps,\s*camera_geometry::kSoftFullWidth,\s*camera_geometry::kSoftFullHeight, 0\);/);
const softLeave = popup.slice(popup.indexOf('static void leave_soft_full_screen('), popup.indexOf('static void leave_full_screen('));
assert.ok(softLeave.indexOf('lv_image_set_src(ctx->soft_full_image, nullptr);') < softLeave.indexOf('camera_stream_stop();'));
assert.match(popup, /if \(g_camera_popup->full\) \{\s*camera_stream_process_ui\(g_camera_popup->soft_full_image, nullptr, nullptr\);/);

// A saved tile is declared to the Bridge even without MQTT routes: a new
// camera tile was "unknown_camera" until a restart (S3 b325).
const tileSave = read('src/web/server/handlers/web_admin_tiles.cpp');
assert.match(tileSave, /MQTT routes unchanged \(no rebuild for style\/layout\)"\);\s*\}[\s\S]{0,300}?entity_search::scheduleTilesReport\(\);/);

console.log('Camera on the S3: software decode, LVGL frames, whole-screen full screen');
