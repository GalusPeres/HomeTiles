// A Camera tile can show the Bridge's picture of its camera or image entity
// instead of the icon (#50, types/camera/picture.h): the head like the Sensor
// tile, the title on a pill mirrored to the circle, one shared pill when they
// would come closer than a grid gap, both at the tile's transparency, no
// shadow and no gradient. This runs the pure head rule and the corner rounding
// on the host, the Web Admin's copy of the rule and its fields in a VM, and
// checks how the panel subscribes, decodes, stores and declares the pictures.
import assert from 'node:assert/strict';
import vm from 'node:vm';

import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

// card width, inset, circle box, corner shrink, title pad, grid gap, title width
const CASES = [
  [194, 4, 64, 2, 12, 16, 76],    // V2 1x1 "Einfahrt": 22 px stay free, split
  [194, 4, 64, 2, 12, 16, 100],   // V2 1x1 "Gast-WLAN": shared
  [194, 4, 64, 2, 12, 16, 400],   // a title longer than the tile: shared, cut
  [404, 4, 64, 2, 12, 16, 112],   // V2 2x1 "Saugroboter": split
  [194, 4, 64, 2, 12, 16, 82],    // exactly one gap free: split
  [194, 4, 64, 2, 12, 16, 83],    // one pixel less: shared
  [111, 3, 45, 1, 8, 10, 40],     // S3 1x1: little room, shared
];

const harness = String.raw`
#include <cstdio>
#include <cstring>
#include "src/ui/screensaver/wallpaper_cover.h"
#include "src/types/camera/picture_layout.h"
using namespace camera_picture_layout;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  const int cases[][7] = {` + CASES.map(c => '{' + c.join(',') + '}').join(',') + String.raw`};
  for (const auto& c : cases) {
    const Head h = head(c[0], c[1], c[2], c[3], c[4], c[5], c[6]);
    std::printf("%s %d %d %d %d\n", h.shared ? "shared" : "split", h.pill_x, h.pill_w, h.text_x, h.text_w);
  }
  CHECK(rgb565(0xFFFFFF) == 0xFFFF && rgb565(0x000000) == 0 && rgb565(0xFF0000) == 0xF800 && rgb565(0x00FF00) == 0x07E0);
  CHECK(blend565(0xFFFF, 0x0000, 16) == 0xFFFF && blend565(0xFFFF, 0x0000, 0) == 0x0000);
  CHECK(blend565(0xF800, 0x001F, 8) == ((16u << 11) | 16u));
  // A 40 x 30 picture, radius 10, rounded towards black: corner pixels go
  // dark, everything outside the four corner squares stays as it was.
  static uint16_t pixels[40 * 30];
  for (auto& p : pixels) p = 0xFFFF;
  round_corners(pixels, 40, 30, 10, 0x0000, wallpaper_cover::rounded_pixel_coverage);
  CHECK(pixels[0] == 0x0000 && pixels[39] == 0x0000 && pixels[29 * 40] == 0x0000 && pixels[29 * 40 + 39] == 0x0000);
  CHECK(pixels[10] == 0xFFFF && pixels[10 * 40] == 0xFFFF && pixels[15 * 40 + 20] == 0xFFFF);
  CHECK(pixels[9 * 40 + 9] == 0xFFFF);  // inside the arc
  int changed = 0;
  for (int y = 0; y < 30; ++y)
    for (int x = 0; x < 40; ++x)
      if (pixels[y * 40 + x] != 0xFFFF) {
        CHECK((x < 10 || x >= 30) && (y < 10 || y >= 20));
        ++changed;
      }
  CHECK(changed > 4 * 15 && changed < 4 * 30);
  // Too small for the radius: untouched.
  static uint16_t small[8 * 8];
  for (auto& p : small) p = 0x1234;
  round_corners(small, 8, 8, 10, 0x0000, wallpaper_cover::rounded_pixel_coverage);
  CHECK(small[0] == 0x1234);
  // Rounded towards black, then the color behind turns light grey: the
  // corners follow, fully outside pixels take the new color, the arc keeps
  // the picture's own color, the rest stays untouched.
  static uint16_t turned[40 * 30];
  for (auto& p : turned) p = 0x07E0;  // green picture
  round_corners(turned, 40, 30, 10, 0x0000, wallpaper_cover::rounded_pixel_coverage);
  reround_corners(turned, 40, 30, 10, 0x0000, 0xE71C, wallpaper_cover::rounded_pixel_coverage);
  static uint16_t fresh[40 * 30];
  for (auto& p : fresh) p = 0x07E0;
  round_corners(fresh, 40, 30, 10, 0xE71C, wallpaper_cover::rounded_pixel_coverage);
  CHECK(turned[0] == 0xE71C && turned[39] == 0xE71C && turned[15 * 40 + 20] == 0x07E0);
  int off = 0;
  for (int i = 0; i < 40 * 30; ++i) {
    const int dg = ((turned[i] >> 5) & 0x3F) - ((fresh[i] >> 5) & 0x3F);
    if (dg > 2 || dg < -2) ++off;
  }
  CHECK(off == 0);
  // The title metric: whole-pixel advances per code point, a line break as a
  // space, multi-byte UTF-8 read as one character.
  size_t at = 0;
  CHECK(next_codepoint("\xC3\xBC" "a", at) == 0xFC && at == 2 && next_codepoint("\xC3\xBC" "a", at) == 'a');
  at = 0;
  CHECK(next_codepoint("\xE2\x82\xAC", at) == 0x20AC && at == 3);
  auto advance = [](uint32_t code) { return code == ' ' ? 5 : code == 0xFC ? 11 : 10; };
  CHECK(title_metric("Tor", advance) == 30);
  CHECK(title_metric("T\xC3\xBCr", advance) == 31);
  CHECK(title_metric("A\nB", advance) == 25);
  CHECK(title_metric("", advance) == 0 && title_metric(nullptr, advance) == 0);
  std::printf("ok\n");
  return 0;
}
`;
const output = compileAndRun({label: 'camera picture head', harness});
let hostLines = null;
if (output !== null) {
  hostLines = output.trim().split('\n');
  assert.equal(hostLines.pop(), 'ok');
  assert.deepEqual(hostLines.map(line => line.split(' ')[0]),
    ['split', 'shared', 'shared', 'split', 'split', 'shared', 'shared']);
  // Split: the pill mirrored into the right corner, as wide as the title.
  assert.equal(hostLines[0], 'split 86 104 100 76');
  // Shared: across the head, the title right of the circle box up to the pad.
  assert.equal(hostLines[1], 'shared 4 186 68 108');
  assert.equal(hostLines[2], 'shared 4 186 68 108');
}

// ---- Web Admin: the same rule, the fields and their stored values ----
const names = ['cameraEvery', 'cameraTransparency', 'cameraPictureOptionsOf', 'cameraPictureOptions',
  'syncCameraFields', 'loadCameraFields', 'saveCameraFields', 'resetCameraFields', 'cameraPictureHead',
  'cameraTitleMetric', 'cameraExtent', 'cameraHeadSurface', 'cameraHeadBackground', 'cameraOptionFlags'];
const elements = new Map();
function element(id, value = '') {
  const hidden = new Set(['hidden']);
  const el = {
    id, value, textContent: '',
    classList: {
      toggle(name, on) { if (on) hidden.add(name); else hidden.delete(name); },
      contains: name => hidden.has(name)
    }
  };
  elements.set(id, el);
  return el;
}
for (const field of ['camera_entity', 'camera_display', 'camera_fit', 'camera_every', 'camera_transparency',
                     'camera_transparency_value', 'camera_picture_fields', 'camera_every_row', 'camera_head',
                     'camera_tap', 'camera_tap_row']) {
  element('t_' + field);
}
const context = {
  document: {getElementById: id => elements.get(id) || null},
  FormData, Math, Number, String,
  CAMERA_FITS: ['fill', 'fit', 'original'],
  CAMERA_TAP_FULL: 1,
  CAMERA_HEAD_LIGHT: 2,
  CAMERA_HEAD_DARK: 4,
  loadIconColorFields() {}, saveIconColorFields() {}, resetIconColorFields() {},
  maybeFillTitleFromEntity() {}, syncSwitchChoices() {}
};
vm.runInNewContext(names.map(extractDeliveredFunction).join('\n'), context);
const value = field => elements.get('t_' + field).value;
// Objects made in the VM have its prototypes.
const plain = object => JSON.parse(JSON.stringify(object));
const hidden = field => elements.get('t_' + field).classList.contains('hidden');

// A stored picture tile: the fields show its options, the picture rows and a
// camera's interval show.
context.loadCameraFields('t', {sensor_entity: 'camera.door', sensor_display_mode: 1, sensor_decimals: 1,
                               image_slideshow_sec: 5, key_code: 30, key_modifier: 3});
assert.deepEqual([value('camera_display'), value('camera_fit'), value('camera_every'), value('camera_transparency'),
                  value('camera_head'), value('camera_tap')],
  ['1', 'fit', '5', '30', 'light', '1']);
assert.equal(hidden('camera_tap_row'), false);
assert.equal(hidden('camera_picture_fields'), false);
assert.equal(hidden('camera_every_row'), false);
assert.equal(elements.get('t_camera_transparency_value').textContent, '30 %');
const form = new FormData();
context.saveCameraFields('t', form);
assert.deepEqual(Object.fromEntries(form.entries()), {
  camera_entity: 'camera.door', sensor_entity: 'camera.door', camera_display: '1', camera_fit: 'fit',
  camera_every: '5', camera_transparency: '30', camera_head: 'light', camera_tap: '1'});
assert.equal(context.cameraOptionFlags({head: 'light', tap: '1'}), 3);
assert.equal(context.cameraOptionFlags({head: 'dark', tap: '0'}), 4);
assert.equal(context.cameraOptionFlags({head: 'auto', tap: '0'}), 0);
// An image entity sends each new picture at once and has no popup: no
// interval, no tap choice.
elements.get('t_camera_entity').value = 'image.vacuum_map';
context.syncCameraFields('t');
assert.equal(hidden('camera_every_row'), true);
assert.equal(hidden('camera_tap_row'), true);
// A tile from before the picture (0xFF decimals come as -1): the icon, fill,
// 10 s, opaque; the picture rows stay hidden.
context.loadCameraFields('t', {sensor_entity: 'camera.door', sensor_display_mode: 0, sensor_decimals: -1,
                               image_slideshow_sec: 10, key_code: 0});
assert.deepEqual([value('camera_display'), value('camera_fit'), value('camera_every'), value('camera_transparency'),
                  value('camera_head'), value('camera_tap')],
  ['0', 'fill', '10', '0', 'auto', '0']);
assert.equal(hidden('camera_picture_fields'), true);
assert.deepEqual(plain(context.cameraPictureOptionsOf({sensor_display_mode: 1, sensor_decimals: 2, image_slideshow_sec: 99,
                                                 key_code: 250})),
  {picture: true, fit: 'original', every: 10, transparency: 100, head: 'auto', tap: '0'});
context.resetCameraFields('t');
assert.deepEqual([value('camera_entity'), value('camera_display'), value('camera_fit'), value('camera_every'),
                  value('camera_transparency'), value('camera_head'), value('camera_tap')],
  ['', '0', 'fill', '10', '0', 'auto', '0']);
if (hostLines) {
  for (const [index, args] of CASES.entries()) {
    const head = context.cameraPictureHead(...args);
    assert.equal([head.shared ? 'shared' : 'split', head.pillX, head.pillW, head.textX, head.textW].join(' '),
      hostLines[index], `the Web Admin head matches the panel for ${args}`);
  }
}

// The preview measures the title with the panel's advances, in display
// pixels, and the card from its column and span like tile_geometry::extent.
const layout = {first: 32, adv: Array.from({length: 0x17F - 31}, (_, i) => (i === 0 ? 5 : 10))};
layout.adv['ü'.codePointAt(0) - 32] = 11;
assert.equal(context.cameraTitleMetric(layout, 'Tür Ost'), 66);
assert.equal(context.cameraExtent(0, 1, 194, 16), 194);
assert.equal(context.cameraExtent(0, 2, 194, 16), 404);
assert.equal(context.cameraExtent(1.5, 1, 194, 16), 194);
// The head follows the design or draws dark or light (user 2026-10-11:
// "Automatisch / Dunkel / Hell", Automatisch by default) at every
// transparency, only with the picture at full height.
assert.equal(context.cameraHeadSurface({picture: true, head: 'auto'}, false), 'auto');
assert.equal(context.cameraHeadBackground('auto'), 'var(--tile-card)');
assert.equal(context.cameraHeadSurface({picture: true, transparency: 0, head: 'dark'}, false), 'dark');
assert.equal(context.cameraHeadSurface({picture: true, transparency: 80, head: 'dark'}, false), 'dark');
assert.equal(context.cameraHeadSurface({picture: true, transparency: 80, head: 'light'}, false), 'light');
assert.equal(context.cameraHeadSurface({picture: true, head: 'dark'}, true), '');
assert.equal(context.cameraHeadSurface({picture: false, head: 'dark'}, false), '');
assert.equal(context.cameraHeadBackground('dark'), 'var(--tile-default-bg)');
assert.equal(context.cameraHeadBackground('light'), 'var(--pvl-card)');

// ---- The panel ----
const options = readRepoFile('src/types/camera/picture.h');
for (const pattern of [
  /return tile\.type == TILE_CAMERA && tile\.sensor_display_mode == kShowPicture &&\s*\(is_camera\(tile\.sensor_entity\) \|\| is_image\(tile\.sensor_entity\)\);/,
  /return tile\.sensor_decimals <= bridge_images::kOriginal \? tile\.sensor_decimals : bridge_images::kFill;/,
  /if \(seconds < bridge_images::kEveryMin\) return bridge_images::kEveryMin;/,
  /return tile\.key_code > kTransparencyMax \? kTransparencyMax : tile\.key_code;/,
]) assert.match(options, pattern);

const handler = readRepoFile('src/types/camera/web_handler.cpp');
assert.match(handler, /tile\.sensor_display_mode =\s*server\.arg\("camera_display"\)\.toInt\(\) == camera_picture::kShowPicture/);
assert.match(handler, /tile\.sensor_decimals = placement == "fit"\s*\? bridge_images::kContain\s*: placement == "original" \? bridge_images::kOriginal\s*: bridge_images::kFill;/);
assert.match(handler, /if \(every < bridge_images::kEveryMin \|\| every > bridge_images::kEveryMax\) every = bridge_images::kEveryDefault;/);
assert.match(handler, /tile\.key_code = static_cast<uint8_t>\(transparency\);/);

const renderer = readRepoFile('src/types/camera/renderer.cpp');
assert.match(renderer, /if \(!compact && grid_type != GridType::SCREENSAVER\) \{\s*camera_picture::attach\(card, icon, title_label, tile\);/,
  'half-height tiles keep the icon look');
assert.match(renderer, /tile\.sensor_entity\.length\(\) &&\s*!camera_picture::is_image\(tile\.sensor_entity\)\) \{\s*CameraEventData\* event_data/,
  'an image entity opens no camera stream');

const picture = readRepoFile('src/types/camera/picture.cpp');
assert.match(picture, /const bool awake = !powerManager\.isInSleep\(\) && !image_screensaver_covers_ui\(\) && !camera_popup_is_busy\(\);/);
assert.match(picture, /if \(camera && !\(awake && on_screen\(tile->card\)\)\) continue;/,
  'a camera still only while its tile is on screen, the display awake and no stream runs');
assert.match(picture, /bridge_images::pictureSuffix\(suffix, sizeof\(suffix\), tile->width, tile->height, tile->fit,\s*camera \? tile->every : 0\)/,
  'the picture in the tile\'s size, a camera at its interval');
assert.match(picture, /show\(\*tile, \*picture\);\s*break;/, 'one decode per loop pass');
assert.match(picture, /tile_icon_disc::mark_surface\(obj\);\s*lv_obj_set_style_bg_opa\(obj, static_cast<lv_opa_t>\(\(kTransparencyMax - transparency\) \* 255 \/ kTransparencyMax\), 0\);/,
  'circle and pill take the controls\' color at the tile\'s transparency');
assert.match(picture, /tile_icon_disc::shrink_corner_disc\(obj\);/);
// The corners follow the color behind the card (a theme change) at once.
assert.match(picture, /\/\/ Every pass: a design change turns the corners with the next frame\.\s*for \(PictureTile\* tile : g_tiles\) follow_corners\(\*tile\);/);
assert.match(picture, /g_parked\.erase\(it\);\s*follow_corners\(tile\);/);
// A rebuilt tile (every Web Admin change) shows its pixels at once, or the
// waiting picture right away, never the icon look in between.
assert.match(picture, /void delete_cb\(lv_event_t\* event\) \{[\s\S]*?park\(\*tile\);/);
assert.match(picture, /if \(!adopt_parked\(\*picture_tile\)\) \{\s*const bridge_images::Picture\* picture = bridge_images::find\(/);
assert.match(picture, /if \(it->fit == tile\.fit\) memcpy\(tile\.key, it->key, sizeof\(tile\.key\)\);/,
  'pixels of another placement stay only until the new picture arrives');
assert.match(picture, /while \(!g_parked\.empty\(\) && now - g_parked\.front\(\)\.since_ms >= kParkedMs\) \{/);
// Panel and preview choose the same head: the title metric, not the kerned width.
assert.match(picture, /text_w = title_metric\(lv_obj_get_style_text_font\(tile\.title, LV_PART_MAIN\), text\);/);
assert.match(picture, /bool head_over_picture\(const Tile& tile\) \{[\s\S]*?return wanted\(tile\) && tile\.span_h >= 1;/,
  'drawn at boot before the link is known: not tied to it');
assert.match(options, /inline bool light_head\(const Tile& tile\) \{ return \(tile\.key_modifier & kHeadLight\) != 0; \}/);
assert.doesNotMatch(picture, /kDarkHeadFrom/, 'no switch at a transparency');
// A tap can open a camera straight in full screen; a tap there closes it to
// the tiles without the popup in between.
assert.match(options, /return tile\.type == TILE_CAMERA && \(tile\.key_modifier & kTapFullScreen\) && is_camera\(tile\.sensor_entity\);/);
assert.match(readRepoFile('src/types/camera/renderer.cpp'), /init\.full_screen = data->full_screen;/);
const popup = readRepoFile('src/ui/popups/camera/camera_popup.cpp');
assert.match(popup, /g_camera_popup->open_full = g_open_next_full \|\| init\.full_screen;\s*g_camera_popup->close_on_leave = init\.full_screen;/);
assert.match(popup, /g_camera_popup->full_request = g_camera_popup->close_on_leave \? kFullClose : kFullLeave;/);
assert.match(popup, /\} else if \(full_request == kFullClose\) \{\s*close_camera_popup\(\);\s*return;\s*\}/);
assert.match(popup, /g_camera_popup->open_full = false;\s*g_camera_popup->close_on_leave = false;/, 'closing forgets it');
// No popup on the way into it (user 2026-10-11): the card stays hidden and
// shows only where the full screen cannot start; closing redraws the screen.
assert.match(popup, /if \(g_camera_popup->shell_skipped\) \{\s*\/\/[^\n]*\n\s*\/\/[^\n]*\n\s*lv_obj_add_flag\(g_camera_popup->card, LV_OBJ_FLAG_HIDDEN\);\s*finish_camera_popup_open\(init\);/);
assert.match(popup, /if \(g_camera_popup->open_full && !g_camera_popup->full\) \{\s*reveal_skipped_popup\(g_camera_popup\);\s*open_popup_stream\(g_camera_popup\);/);
assert.match(popup, /if \(shell_shown\) \{\s*hide_popup_shell\(g_camera_popup->card\);\s*\} else \{[\s\S]*?lv_obj_invalidate\(lv_screen_active\(\)\);/);
assert.match(handler, /tile\.key_modifier = static_cast<uint8_t>\(\(server\.arg\("camera_tap"\) == "1" \? camera_picture::kTapFullScreen : 0\) \|\s*\(server\.arg\("camera_head"\) == "light"  \? camera_picture::kHeadLight\s*: server\.arg\("camera_head"\) == "dark" \? camera_picture::kHeadDark\s*: 0\)\);/);
const tiles = readRepoFile('src/tiles/runtime/tile_renderer.cpp');
assert.match(tiles, /if \(grid_type != GridType::SCREENSAVER && camera_picture::head_over_picture\(tile\)\) \{\s*return camera_picture::light_head\(tile\)  \? tile_surface::kLight\s*: camera_picture::dark_head\(tile\) \? tile_surface::kDark\s*: tile_surface::kNone;/,
  'the head follows the design or draws on its fixed surface');
assert.match(tiles, /tile_has_own_color\(tile\) \|\| camera_picture::head_over_picture\(tile\)\) \{\s*return 0;/);
assert.match(readRepoFile('src/types/camera/renderer.cpp'),
  /const uint32_t card_color = camera_picture::head_over_picture\(tile\)\s*\? tileDefaultBgColor\(\)/);
const scripts = readRepoFile('src/types/camera/web_scripts.cpp');
assert.match(scripts, /for \(uint32_t code = 0x20; code <= 0x17F; \+\+code\) \{/);
assert.match(scripts, /lv_font_get_glyph_width\(font, code, 0\)/, 'advances without kerning, like title_metric');
assert.match(scripts, /const lv_font_t\* font = tile_layout::header_title_font\(\);/);
assert.match(picture, /camera_picture_layout::round_corners\(pixels, w, h,/, 'the corners are part of the picture, no clipping layer');
assert.doesNotMatch(picture, /shadow|grad|clip_corner/, 'no shadow, no gradient, no clipping layer');
assert.match(picture, /dsc->header\.cf = LV_COLOR_FORMAT_RGB565;/);
assert.match(readRepoFile('src/ui/screensaver/image_screensaver.cpp'),
  /pixels = hw_decode_jpeg\(data, len, out_w, out_h, native, want_w, want_h\);[\s\S]*?pixels = sw_decode_jpeg\(data, len, out_w, out_h\);/,
  'tiles share the screensaver\'s decoders and hardware engine');

const loop = readRepoFile('HomeTiles.ino');
assert.equal((loop.match(/camera_picture::service\(\);/g) || []).length, 2, 'awake and asleep (drops the camera pictures)');
// Under an open tile popup no picture is decoded (user 2026-10-14: the
// Weather popup turned slower with a camera tile on Home, every picture
// redrew the popup over it); the newest one shows when it closes.
{
  const service = picture.slice(picture.indexOf('void service() {'));
  assert.match(service, /for \(PictureTile\* tile : g_tiles\) follow_corners\(\*tile\);[\s\S]{0,400}if \(popup_shell_active\(\)\) return;\s*\/\/ One decode per call/,
    'No decode under an open popup');
}

const mqtt = readRepoFile('src/network/mqtt/mqtt_handlers.cpp');
assert.match(mqtt, /for \(const String& topic : g_tile_picture_topics\) networkManager\.mqttEnqueueSubscribe\(topic\.c_str\(\)\);/,
  'the tile pictures come back after a reconnect');
assert.match(mqtt, /if \(old != g_live_picture_topic && std::find\(topics\.begin\(\), topics\.end\(\), old\) == topics\.end\(\)\) \{\s*networkManager\.mqttEnqueueUnsubscribe/);

assert.match(readRepoFile('src/network/bridge/entity_search.cpp'),
  /if \(type == TILE_CAMERA && entity && strncmp\(entity, "image\.", 6\) == 0\) return "images";/,
  'the Bridge serves an image entity only from its images list');

// ---- Texts and the Web Admin ----
const strings = readRepoFile('src/core/i18n/i18n.cpp');
for (const text of ['"Transparenz von Kreis und Titel"', '"Circle and title transparency"',
                    '"Transparence du cercle et du titre"', '"Przezroczystość koła i tytułu"']) {
  assert.ok(strings.includes(text), text);
}
const html = readRepoFile('src/types/camera/web_html.cpp');
assert.match(html, /appendEntityPickerField\(html, tab_id, "camera_entity", tr\.screensaver_picture_entity, "images"\);/);
assert.match(html, /append_switch_choice\(html, tab_id, "camera_display", tr\.camera_show, shows, 2\);/);
assert.match(html, /append_switch_choice\(html, tab_id, "camera_fit", tr\.screensaver_picture_fit, fits, 3\);/);
assert.match(html, /_camera_transparency" min="0" max="100" step="5" value="0">/);
assert.match(html, /append_switch_choice\(html, tab_id, "camera_head", tr\.camera_head, heads, 3\);/);
assert.match(html, /append_switch_choice\(html, tab_id, "camera_tap", tr\.camera_tap, taps, 2\);/);
assert.doesNotMatch(html, /class="hint"|screensaver_picture_hint/, 'no hint text under the options');
assert.match(readRepoFile('src/types/switch/admin.js'), /'camera_display', 'camera_fit',\s*'camera_head', 'camera_tap', 'switch_orientation', 'fan_orientation'\];/);
assert.match(readRepoFile('src/web/admin/tiles/live-preview.js'),
  /applyCameraPicturePreview\(tileElem, cameraPictureOptions\(tab\), halfHeight, \{\s*col: Number\(tileElem\.dataset\.col \|\| 0\),\s*span_w: tileSpan\('span_w'\)\}\);/);
assert.match(readRepoFile('src/web/admin/tiles/grid-preview.js'),
  /applyCameraPicturePreview\(el, cameraPictureOptionsOf\(tile\), Number\(tile\.span_h\) === 0\.5, tile\);/);
assert.match(readRepoFile('src/web/admin/tiles/snapshots.js'),
  /tile\.key_code = cameraTransparency\(snapshot\.camera_transparency\);/, 'a draft keeps the options');
assert.match(readRepoFile('src/web/admin/tiles/import-export.js'),
  /fd\.append\('camera_transparency', String\(picture\.transparency\)\);/, 'an import keeps the options');
assert.match(readRepoFile('src/web/server/render/web_admin_styles.cpp'),
  /emit_scaled\("camera-title-pad", tile_layout::scale_480\(12\)\);/);
const css = readRepoFile('src/web/assets/admin.css');
assert.match(css, /\.tile\.camera-picture > \.tile-title::before \{[^}]*background:var\(--control-fill[^}]*opacity:var\(--camera-head-opa, 1\);/);
assert.match(css, /\.tile\.camera\.camera-picture \{\s*display:block;\s*background-image:linear-gradient\(var\(--camera-picture-standin, #5c6066\), var\(--camera-picture-standin, #5c6066\)\) !important;/,
  'the stand-in is a background layer, no extra layer below the head');
assert.doesNotMatch(css, /\.tile\.camera\.camera-picture::before/);
const live = readRepoFile('src/web/admin/tiles/live-preview.js');
assert.match(live, /const cameraHead = meta\.preview === 'camera' \? cameraHeadSurface\(cameraPictureOptions\(tab\), halfHeight\) : '';\s*if \(cameraHead\) tileElem\.style\.background = cameraHeadBackground\(cameraHead\);/);
assert.match(readRepoFile('src/web/admin/tiles/grid-preview.js'),
  /el\.style\.background = cameraHead \? cameraHeadBackground\(cameraHead\) : bg;/);
// Computed in a browser in test-camera-head-css.mjs (the selector must win
// over the :is() size rule of icons above a title).
assert.match(css, /\.tile\.camera\.camera-picture\.camera-head-shared:not\(\.empty\):not\(\.sensor-compact\) > \.tile-icon::after \{ opacity:0; \}/);

console.log('Camera tile picture: Sensor head with a title pill (shared when close), transparency, tile-size pictures');
