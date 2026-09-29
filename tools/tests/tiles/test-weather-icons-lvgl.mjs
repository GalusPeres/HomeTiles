// Colored weather icons: the generated table and fonts match the parts, sit
// exactly on the MDI glyph box, and the weather tile and popup draw every
// condition icon through them (regression: the weather icons were the plain
// white MDI outlines).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {lvglFontSource} from '../../lib/lvgl-font-source.mjs';
import {WEATHER_ICON_CODEPOINTS, weatherIconTableSource} from '../../generate-weather-icon-fonts.mjs';
import {ICONS} from '../../weather-icons/parts.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (p) => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find((f) => f.name === name);
  assert(found, name);
  return found.source;
};

// The table is generated from the parts.
assert.equal(read('src/types/weather/weather_icon_table.h'), weatherIconTableSource(),
  'weather_icon_table.h is stale: run node tools/generate-weather-icon-fonts.mjs');

// Fonts: MDI line box and advance, MDI fallback, zero-advance layers, and the
// filled sun exactly on the MDI weather-sunny box.
const MDI_WEATHER_SUNNY = 0xF0599;
const {first, last, layers} = WEATHER_ICON_CODEPOINTS;
assert.equal(last - first, layers);
for (const size of [32, 40, 48]) {
  const weather = lvglFontSource(read(`src/fonts/weather_icons_${size}.c`));
  const mdi = lvglFontSource(read(`src/fonts/mdi_icons_${size}.c`));
  assert.equal(weather.lineHeight, mdi.lineHeight, `${size}: line height`);
  assert.equal(weather.baseLine, mdi.baseLine, `${size}: base line`);
  assert.equal(weather.fallback, `mdi_icons_${size}`, `${size}: MDI fallback`);
  const mdiSunny = mdi.glyph(MDI_WEATHER_SUNNY);
  assert.equal(weather.glyph(first).advW, mdiSunny.advW, `${size}: spacer advance`);
  for (let codepoint = first + 1; codepoint <= last; codepoint++) {
    const glyph = weather.glyph(codepoint);
    assert(glyph && glyph.boxW > 0 && glyph.boxH > 0, `${size}: layer U+${codepoint.toString(16)}`);
    assert.equal(glyph.advW, 0, `${size}: layer U+${codepoint.toString(16)} has no advance`);
  }
  const sunny = weather.glyph(first + 1);
  for (const key of ['boxW', 'boxH', 'ofsX', 'ofsY']) {
    assert.equal(sunny[key], mdiSunny[key], `${size}: filled sun ${key} equals MDI weather-sunny`);
  }
}
assert.match(weatherIconTableSource(), /\{"weather-sunny", "#FFC53D " "\\xEE\\x80\\x81" "#" "\\xEE\\x80\\x80"\}/);

// Every Home Assistant condition resolves to a colored icon.
const popup = read('src/ui/popups/weather/weather_popup.cpp');
const tileRenderer = read('src/tiles/runtime/tile_renderer.cpp');
for (const source of [popup, tileRenderer]) {
  const names = [...fn(source, 'weather_icon_from_condition').matchAll(/return "mdi:([\w-]+)"/g)].map((m) => m[1]);
  assert.equal(names.length, 15);
  for (const name of names) assert(ICONS[name], `condition icon ${name} has colored layers`);
}

// The tile and popup show condition icons through weather_icons.
const renderer = read('src/types/weather/renderer.cpp');
const tileBuild = fn(renderer, 'render_weather_tile');
assert.equal(tileBuild.match(/weather_icons::style_label\(/g)?.length, 2, 'tile header and forecast icons');
assert.doesNotMatch(tileBuild, /getMdiChar\(/);
const tileState = fn(tileRenderer, 'update_weather_tile_state');
assert.equal(tileState.match(/weather_icons::text\(/g)?.length, 2, 'tile state header and forecast icons');
assert.doesNotMatch(tileState, /getMdiChar\(/);
for (const name of ['update_forecast_graph', 'update_detail_view', 'apply_weather_header']) {
  const body = fn(popup, name);
  assert.match(body, /weather_icons::text\(/, name);
  assert.doesNotMatch(body, /getMdiChar\(/, name);
}
assert.equal(fn(popup, 'build_popup_ui').match(/weather_icons::style_label\(/g)?.length, 4,
  'popup header, day, hour and now icons');
// The shared popup header copies the recoloring, and the tile icon disc
// measures the icon without the recolor commands.
assert.match(fn(read('src/ui/popups/popup_shell.cpp'), 'copy_label'), /lv_label_set_recolor\(target, lv_label_get_recolor\(source\)\)/);
assert.match(fn(read('src/tiles/runtime/tile_icon_disc.h'), 'add_round'), /lv_label_get_recolor\(icon\) \? LV_TEXT_FLAG_RECOLOR/);

const host = await lvglHost(root);
if (!host) {
  console.log('Weather icon table and fonts match; SKIP: rendering needs LVGL and a host compiler');
  process.exit(0);
}

// Rendering: the partly cloudy icon draws a yellow sun and a light cloud in
// one MDI-wide label, the filled sun covers the MDI sun box, and other MDI
// icons render unchanged through the fallback.
const strip = (s) => s.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
const out = path.join(root, 'build/tests/weather-icons-lvgl');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <lvgl.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
extern "C" { LV_FONT_DECLARE(mdi_icons_48); }
class String : public std::string {
 public:
  using std::string::string;
  String() = default;
  String(const std::string& s) : std::string(s) {}
};
static String utf8(uint32_t c) {
  std::string s;
  s += char(0xF0 | (c >> 18)); s += char(0x80 | ((c >> 12) & 0x3F));
  s += char(0x80 | ((c >> 6) & 0x3F)); s += char(0x80 | (c & 0x3F));
  return s;
}
String normalizeMdiIconName(const String& name) { return name.rfind("mdi:", 0) == 0 ? String(name.substr(4)) : name; }
String getMdiChar(const String& name) {
  if (name == "home") return utf8(0xF02DC);
  if (name == "weather-sunny") return utf8(0xF0599);
  return "";
}
${strip(read('src/types/weather/weather_icon_table.h'))}
${strip(read('src/types/weather/weather_icons.h'))}
${strip(read('src/types/weather/weather_icons.cpp'))}
static std::vector<uint32_t> pixels(400 * 100);
struct Box { int x1 = 1 << 30, y1 = 1 << 30, x2 = -1, y2 = -1, sun = 0, cloud = 0, lit = 0; };
static Box scan(int x0) {
  Box b;
  for (int y = 0; y < 100; ++y) for (int x = x0; x < x0 + 100; ++x) {
    const uint32_t p = pixels[y * 400 + x];
    const int r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, bl = p & 0xFF;
    if (r + g + bl < 60) continue;
    ++b.lit;
    if (x < b.x1) b.x1 = x; if (y < b.y1) b.y1 = y; if (x > b.x2) b.x2 = x; if (y > b.y2) b.y2 = y;
    if (r > 230 && g > 170 && g < 220 && bl < 90) ++b.sun;
    if (r > 225 && g > 230 && bl > 235) ++b.cloud;
  }
  return b;
}
int main() {
  lv_init();
  lv_display_t* display = lv_display_create(400, 100);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display, pixels.data(), nullptr, pixels.size() * 4, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  lv_obj_t* screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  auto label = [&](int x, const lv_font_t* font, const String& text, bool weather) {
    lv_obj_t* l = lv_label_create(screen);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_font(l, font, 0);
    if (weather) weather_icons::style_label(l);
    lv_label_set_text(l, text.c_str());
    lv_obj_set_pos(l, x + 20, 20);
    return l;
  };
  lv_obj_t* partly = label(0, &mdi_icons_48, weather_icons::text("mdi:weather-partly-cloudy"), true);
  label(100, &mdi_icons_48, weather_icons::text("weather-sunny"), true);
  label(200, &mdi_icons_48, getMdiChar("weather-sunny"), false);
  label(300, &mdi_icons_48, weather_icons::text("home"), true);
  lv_refr_now(display);
  const Box a = scan(0), sun = scan(100), outline = scan(200), home = scan(300);
  std::vector<uint32_t> home_weather;
  for (int y = 0; y < 100; ++y) for (int x = 300; x < 400; ++x) home_weather.push_back(pixels[y * 400 + x]);
  lv_obj_set_style_text_font(lv_obj_get_child(screen, 3), &mdi_icons_48, 0);
  lv_label_set_recolor(lv_obj_get_child(screen, 3), false);
  lv_refr_now(display);
  int home_diff = 0, i = 0;
  for (int y = 0; y < 100; ++y) for (int x = 300; x < 400; ++x) home_diff += pixels[y * 400 + x] != home_weather[i++];
  std::printf("{\"width\":%d,\"mdiAdvance\":%d,\"height\":%d,\"lineHeight\":%d,"
              "\"sunPixels\":%d,\"cloudPixels\":%d,"
              "\"sunBox\":[%d,%d,%d,%d],\"outlineBox\":[%d,%d,%d,%d],\"sunYellow\":%d,"
              "\"homeLit\":%d,\"homeDiff\":%d}\n",
              (int)lv_obj_get_width(partly), (int)lv_font_get_glyph_width(&mdi_icons_48, 0xF0599, 0),
              (int)lv_obj_get_height(partly), (int)lv_font_get_line_height(&mdi_icons_48),
              a.sun, a.cloud, sun.x1 - 100, sun.y1, sun.x2 - 100, sun.y2,
              outline.x1 - 200, outline.y1, outline.x2 - 200, outline.y2, sun.sun, home.lit, home_diff);
  return 0;
}
`;
const source = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(source, cpp);
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', source, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
const r = JSON.parse(result.stdout.trim().split('\n').at(-1));
assert.equal(r.width, r.mdiAdvance, 'a colored icon is exactly one MDI glyph wide');
assert.equal(r.height, r.lineHeight, 'and one MDI line high');
assert(r.sunPixels > 40, `partly cloudy draws a yellow sun (${r.sunPixels})`);
assert(r.cloudPixels > 200, `partly cloudy draws a light cloud (${r.cloudPixels})`);
assert(r.sunYellow > 300, `sunny is yellow (${r.sunYellow})`);
assert.deepEqual(r.sunBox, r.outlineBox, 'the filled sun covers the MDI weather-sunny box');
assert(r.homeLit > 100 && r.homeDiff === 0, 'other icons render unchanged through the MDI fallback');
console.log(`Weather icons: ${Object.keys(ICONS).length} colored icons, ${layers} layers; ` +
  `render ${JSON.stringify(r)}`);
