// Colored weather icons: the generated table and fonts match the parts, sit
// exactly on the MDI glyph box, follow the weather color system, switch to
// night icons from the bridge sun times, and the weather tile and popup draw
// every condition icon through them unless the tile setting turns them off
// (regression: the weather icons were the plain white MDI outlines).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {lvglFontSource} from '../../lib/lvgl-font-source.mjs';
import {WEATHER_ICON_CODEPOINTS, weatherIconTableSource} from '../../generate-weather-icon-fonts.mjs';
import {ICONS, TINTS} from '../../weather-icons/parts.mjs';

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

// Weather colors: five sky base tones, a mixed condition takes the midpoint of
// its parts, heavier weather a deeper tone.
const rgb = (hex) => [1, 3, 5].map((i) => parseInt(hex.slice(i, i + 2), 16));
const midpoint = (a, b) => rgb(a).map((v, i) => (v + rgb(b)[i]) / 2);
for (const [mixed, a, b] of [
  ['weather-partly-cloudy', 'weather-sunny', 'weather-cloudy'],
  ['weather-night-partly-cloudy', 'weather-night', 'weather-cloudy'],
  ['weather-snowy-rainy', 'weather-rainy', 'weather-snowy'],
  ['weather-lightning-rainy', 'weather-lightning', 'weather-rainy'],
  ['weather-windy-variant', 'weather-windy', 'weather-cloudy'],
]) {
  rgb(TINTS[mixed]).forEach((v, i) => assert(Math.abs(v - midpoint(TINTS[a], TINTS[b])[i]) <= 1,
    `${mixed} is the midpoint of ${a} and ${b}`));
}
const luminance = (hex) => rgb(hex).reduce((sum, v, i) => sum + v * [0.2126, 0.7152, 0.0722][i], 0);
assert(luminance(TINTS['weather-pouring']) < luminance(TINTS['weather-rainy']), 'pouring is deeper than rain');
const [r, g, b] = rgb(TINTS['weather-lightning']);
assert(b > r && r > g, 'lightning is purple');
const [nr, ng, nb] = rgb(TINTS['weather-night']);
assert(nb > ng && ng > nr && nb - nr < 64, 'night is grey blue');
assert.deepEqual(Object.keys(TINTS).sort(), Object.keys(ICONS).sort(), 'every icon has a weather color');

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

// Every Home Assistant condition resolves to a colored icon, and the day icons
// Home Assistant also reports at night have night variants.
const popup = read('src/ui/popups/weather/weather_popup.cpp');
const tileRenderer = read('src/tiles/runtime/tile_renderer.cpp');
for (const source of [popup, tileRenderer]) {
  const names = [...fn(source, 'weather_icon_from_condition').matchAll(/return "mdi:([\w-]+)"/g)].map((m) => m[1]);
  assert.equal(names.length, 15);
  for (const name of names) assert(ICONS[name], `condition icon ${name} has colored layers`);
}
assert(ICONS['weather-night-partly-cloudy'] && ICONS['weather-night']);

// Weather tile setting: 0 colored (default and older tiles), 1 white outlines.
assert.match(read('src/tiles/config/tile_config.h'),
  /weatherColoredIcons\(const Tile& tile\) \{\s*return tile\.type != TILE_WEATHER \|\| tile\.sensor_display_mode != 1;/);
const handler = read('src/types/weather/web_handler.cpp');
assert.match(handler, /server\.arg\("weather_colored_icons"\)\.toInt\(\) == 0 \? 1 : 0/);
// The setting sits in the icon section (weather only), not between the
// weather fields.
const html = read('src/web/server/render/web_admin_html.cpp');
assert.doesNotMatch(read('src/types/weather/web_html.cpp'), /weather_colored_icons/);
assert.match(html, /_tile_icon_glow" checked> \)html";[\s\S]{0,200}if \(!screensaver_mode\) \{\s*html \+= R"html\(\s*<label class="inline-checkbox hidden" id="\)html";\s*html \+= tab_id;\s*html \+= R"html\(_weather_colored_icons_row"><input type="checkbox" id="\)html";\s*html \+= tab_id;\s*html \+= R"html\(_weather_colored_icons" checked> \)html";\s*appendHtmlEscaped\(html, tr\.weather_colored_icons\);/);
assert.match(read('src/web/admin/tiles/snapshots.js'),
  /getElementById\(tab \+ '_weather_colored_icons_row'\)\?\.classList\.toggle\('hidden', !weather\)/);
const i18n = read('src/core/i18n/i18n.cpp');
for (const label of ['"Farbige Wetter-Icons"', '"Colored weather icons"', '"Icônes météo en couleur"']) {
  assert(i18n.includes(label), `translation ${label}`);
}
assert.match(read('src/core/i18n/i18n.h'), /const char\* weather_colored_icons;/);

// The tile and popup draw condition icons through weather_icons.
const renderer = read('src/types/weather/renderer.cpp');
const tileBuild = fn(renderer, 'render_weather_tile');
assert.equal(tileBuild.match(/weather_icons::style_label\(/g)?.length, 2, 'tile header and forecast icons');
assert.match(tileBuild, /weather_icons::text\(icon_name, weatherColoredIcons\(tile\)/);
assert.match(tileBuild, /init\.icon_forced = tile_icon_disc::forced_color\(icon, forced\);/);
assert.match(tileBuild, /init\.colored_icons = data->colored_icons;/);
assert.doesNotMatch(tileBuild, /getMdiChar\(/);
const tileState = fn(tileRenderer, 'update_weather_tile_state');
assert.equal(tileState.match(/weather_icons::text\(/g)?.length, 2, 'tile state header and forecast icons');
assert.match(tileState, /weather_icons::parse_sun\(json\.c_str\(\), sun\);\s*icon_name = weather_icons::for_now\(icon_name, sun\);/);
assert.match(tileState, /icon_forced\s*\? weather_icons::Style::Single/);
assert.match(tileState, /tile_icon_disc::set_icon_color\(widgets\.icon_label, lv_color_hex\(tint \? tint : ui_theme::text\(\)\)\)/);
assert.doesNotMatch(tileState, /getMdiChar\(/);
for (const name of ['update_forecast_graph', 'update_detail_view']) {
  const body = fn(popup, name);
  assert.match(body, /weather_icons::text\([^;]*forecast_icon_style\(ctx\)\)/, name);
  assert.doesNotMatch(body, /getMdiChar\(/, name);
}
const header = fn(popup, 'apply_weather_header');
assert.match(header, /weather_icons::parse_sun\(json\.c_str\(\), ctx->sun\);\s*icon_name = weather_icons::for_now\(icon_name, ctx->sun\);/);
assert.match(header, /weather_icons::text\(icon_name, header_icon_style\(ctx\)\)/);
assert.match(fn(popup, 'parse_hourly_weather_object'),
  /weather_icons::is_night\(ctx->sun, hour\.date_local\.c_str\(\), hour\.hour_local \* 60 \+ 30\)/);
assert.match(fn(popup, 'finish_weather_popup_open'),
  /rendered_icon_key ==\s*icon_style_key\(init\.colored_icons, init\.icon_forced\)/);
assert.equal(fn(popup, 'build_popup_ui').match(/weather_icons::style_label\(/g)?.length, 4,
  'popup header, day, hour and now icons');
// A rule's icon color switches the tile icon to single-color layers.
assert.match(fn(read('src/tiles/runtime/tile_icon_source.cpp'), 'refresh_card'),
  /tile\.type == TILE_WEATHER && weatherColoredIcons\(tile\)\) \{\s*weather_icons::follow_icon_color\(icon, \(colored && layer\.icon\) \|\| force_fixed\);/);
// The shared popup header copies the recoloring, and the tile icon disc
// measures the icon without the recolor commands.
assert.match(fn(read('src/ui/popups/popup_shell.cpp'), 'copy_label'), /lv_label_set_recolor\(target, lv_label_get_recolor\(source\)\)/);
assert.match(fn(read('src/tiles/runtime/tile_icon_disc.h'), 'add_round'), /lv_label_get_recolor\(icon\) \? LV_TEXT_FLAG_RECOLOR/);

const host = await lvglHost(root);
if (!host) {
  console.log('Weather icon table, colors and wiring match; SKIP: rendering needs LVGL and a host compiler');
  process.exit(0);
}

// Rendering and runtime behavior.
const strip = (s) => s.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
const out = path.join(root, 'build/tests/weather-icons-lvgl');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <lvgl.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
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
  if (name == "weather-partly-cloudy") return utf8(0xF0595);
  return "";
}
static struct tm g_now {};
bool getLocalTime(struct tm* out, uint32_t) { *out = g_now; return true; }
${strip(read('src/types/weather/weather_icon_table.h'))}
${strip(read('src/types/weather/weather_icons.h'))}
${strip(read('src/types/weather/weather_icons.cpp'))}
static std::vector<uint32_t> pixels(400 * 100);
struct Box { int x1 = 1 << 30, y1 = 1 << 30, x2 = -1, y2 = -1, sun = 0, cloud = 0, lit = 0, colored = 0; };
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
    if (r - bl > 40 || bl - r > 40) ++b.colored;
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
  auto label = [&](int x, const String& text, bool weather) {
    lv_obj_t* l = lv_label_create(screen);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_font(l, &mdi_icons_48, 0);
    if (weather) weather_icons::style_label(l);
    lv_label_set_text(l, text.c_str());
    lv_obj_set_pos(l, x + 20, 20);
    return l;
  };
  using weather_icons::Style;
  lv_obj_t* partly = label(0, weather_icons::text("mdi:weather-partly-cloudy"), true);
  label(100, weather_icons::text("weather-sunny"), true);
  label(200, getMdiChar("weather-sunny"), false);
  lv_obj_t* home = label(300, weather_icons::text("home"), true);
  lv_refr_now(display);
  const Box a = scan(0), sun = scan(100), outline = scan(200), home_box = scan(300);
  std::vector<uint32_t> home_weather;
  for (int y = 0; y < 100; ++y) for (int x = 300; x < 400; ++x) home_weather.push_back(pixels[y * 400 + x]);
  lv_obj_set_style_text_font(home, &mdi_icons_48, 0);
  lv_label_set_recolor(home, false);
  lv_refr_now(display);
  int home_diff = 0, i = 0;
  for (int y = 0; y < 100; ++y) for (int x = 300; x < 400; ++x) home_diff += pixels[y * 400 + x] != home_weather[i++];
  // A forced rule color: all layers in the label color; released: colors again.
  lv_obj_set_style_text_color(partly, lv_color_white(), 0);
  weather_icons::follow_icon_color(partly, true);
  lv_refr_now(display);
  const Box single = scan(0);
  const bool single_text = strcmp(lv_label_get_text(partly), weather_icons::text("weather-partly-cloudy", Style::Single).c_str()) == 0;
  weather_icons::follow_icon_color(partly, false);
  const bool colored_again = strcmp(lv_label_get_text(partly), weather_icons::text("weather-partly-cloudy").c_str()) == 0;
  // Sun times: day and night by local date and minute, polar days, and the
  // night variants of the day icons.
  weather_icons::SunTimes sun_times;
  const bool parsed = weather_icons::parse_sun(
      R"({"state":"partlycloudy","forecast":[{"condition":"sunny"}],"sun":[{"d":"2026-09-29","r":432,"s":1145},{"d":"2026-12-21","up":false},{"d":"2026-06-21","up":true}]})",
      sun_times);
  const bool day = !weather_icons::is_night(sun_times, "2026-09-29", 12 * 60);
  const bool before_rise = weather_icons::is_night(sun_times, "2026-09-29", 431);
  const bool at_set = weather_icons::is_night(sun_times, "2026-09-29", 1145);
  const bool polar = weather_icons::is_night(sun_times, "2026-12-21", 12 * 60) &&
                     !weather_icons::is_night(sun_times, "2026-06-21", 0);
  const bool unknown_day = !weather_icons::is_night(sun_times, "2026-10-01", 0);
  // The bridge writes json.dumps separators (spaces after ':' and ',').
  weather_icons::SunTimes bridge;
  const bool bridge_format = weather_icons::parse_sun(
      R"({"forecast": [{"date_local": "2026-09-29"}], "forecast_hourly": [], "sun": [{"d": "2026-09-29", "r": 432, "s": 1145}, {"d": "2026-12-21", "up": false}, {"d": "2026-06-21", "r": 0, "s": 1440}]})",
      bridge) && bridge.count == 3 &&
      weather_icons::is_night(bridge, "2026-09-29", 1200) && !weather_icons::is_night(bridge, "2026-09-29", 600) &&
      weather_icons::is_night(bridge, "2026-12-21", 600) && !weather_icons::is_night(bridge, "2026-06-21", 1439);
  weather_icons::SunTimes none;
  const bool no_sun = !weather_icons::parse_sun(R"({"state":"sunny","icon":"mdi:weather-sunny"})", none);
  g_now.tm_year = 126; g_now.tm_mon = 8; g_now.tm_mday = 29; g_now.tm_hour = 22; g_now.tm_min = 5;
  const bool night_now = weather_icons::for_now("weather-partly-cloudy", sun_times) == "weather-night-partly-cloudy" &&
                         weather_icons::for_now("mdi:weather-sunny", sun_times) == "weather-night" &&
                         weather_icons::for_now("weather-rainy", sun_times) == "weather-rainy";
  g_now.tm_hour = 12;
  const bool day_now = weather_icons::for_now("weather-partly-cloudy", sun_times) == "weather-partly-cloudy" &&
                       weather_icons::for_now("weather-partly-cloudy", none) == "weather-partly-cloudy";
  const bool outline_style = weather_icons::text("weather-sunny", Style::Outline) == getMdiChar("weather-sunny");
  const bool tints = weather_icons::tint("mdi:weather-lightning") == 0x8B5CF6 && weather_icons::tint("home") == 0;
  std::printf("{\"width\":%d,\"mdiAdvance\":%d,\"height\":%d,\"lineHeight\":%d,"
              "\"sunPixels\":%d,\"cloudPixels\":%d,"
              "\"sunBox\":[%d,%d,%d,%d],\"outlineBox\":[%d,%d,%d,%d],\"sunYellow\":%d,"
              "\"homeLit\":%d,\"homeDiff\":%d,\"singleColored\":%d,\"singleLit\":%d,"
              "\"singleText\":%d,\"coloredAgain\":%d,\"parsed\":%d,\"day\":%d,\"beforeRise\":%d,"
              "\"atSet\":%d,\"polar\":%d,\"unknownDay\":%d,\"noSun\":%d,\"nightNow\":%d,\"dayNow\":%d,"
              "\"outlineStyle\":%d,\"tints\":%d,\"bridgeFormat\":%d}\n",
              (int)lv_obj_get_width(partly), (int)lv_font_get_glyph_width(&mdi_icons_48, 0xF0599, 0),
              (int)lv_obj_get_height(partly), (int)lv_font_get_line_height(&mdi_icons_48),
              a.sun, a.cloud, sun.x1 - 100, sun.y1, sun.x2 - 100, sun.y2,
              outline.x1 - 200, outline.y1, outline.x2 - 200, outline.y2, sun.sun, home_box.lit, home_diff,
              single.colored, single.lit, single_text, colored_again, parsed, day, before_rise, at_set,
              polar, unknown_day, no_sun, night_now, day_now, outline_style, tints, bridge_format);
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
const run = JSON.parse(result.stdout.trim().split('\n').at(-1));
assert.equal(run.width, run.mdiAdvance, 'a colored icon is exactly one MDI glyph wide');
assert.equal(run.height, run.lineHeight, 'and one MDI line high');
assert(run.sunPixels > 40, `partly cloudy draws a yellow sun (${run.sunPixels})`);
assert(run.cloudPixels > 200, `partly cloudy draws a light cloud (${run.cloudPixels})`);
assert(run.sunYellow > 300, `sunny is yellow (${run.sunYellow})`);
assert.deepEqual(run.sunBox, run.outlineBox, 'the filled sun covers the MDI weather-sunny box');
assert(run.homeLit > 100 && run.homeDiff === 0, 'other icons render unchanged through the MDI fallback');
assert(run.singleLit > 400 && run.singleColored === 0 && run.singleText, 'a forced color draws all layers in it');
assert(run.coloredAgain, 'releasing the rule color restores the weather colors');
for (const key of ['parsed', 'day', 'beforeRise', 'atSet', 'polar', 'unknownDay', 'noSun', 'nightNow', 'dayNow',
  'outlineStyle', 'tints', 'bridgeFormat']) {
  assert.equal(run[key], 1, key);
}
console.log(`Weather icons: ${Object.keys(ICONS).length} colored icons, ${layers} layers; render ${JSON.stringify(run)}`);
