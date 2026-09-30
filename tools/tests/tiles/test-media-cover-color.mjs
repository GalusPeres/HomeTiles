// Media tiles offer icon color and tile color "From cover": the most
// prominent saturated hue of the album cover (media/cover_color.h), stored as
// the record line "cover [icon] [tile=NN]" on Media tiles only. The icon takes
// the color like a forced rule color, the tile the tint like "From icon"
// (readable, tile_tint.h), both below an active rule; grey, white and black
// covers keep the tile neutral. Every other tile type and every existing
// record keeps its path unchanged.
import assert from 'node:assert/strict';
import vm from 'node:vm';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// Records: [input, normalized for Media, normalized for other types].
const records = [
  ['v2\n\ncover icon', 'v2\n\ncover icon', ''],
  ['v2\n\ncover tile=30', 'v2\n\ncover tile=30', ''],
  ['v2\n\ncover icon tile=99', 'v2\n\ncover icon tile=50', ''],
  ['v2\n\ncover tile=3 icon', 'v2\n\ncover icon tile=10', ''],
  ['v2\n\ncover bogus', '', ''],
  ['v2\n\ncover', '', ''],
  ['v2\nFF0000\nsrc auto self tile=20\ncover icon\nfill 25', 'v2\nFF0000\nfill 25\ncover icon\nsrc auto self tile=20',
    'v2\nFF0000\nfill 25\nsrc auto self tile=20'],
  ['v2\n\nfill 20', 'v2\n\nfill 20', 'v2\n\nfill 20'],
];

// Browser mirror (icon-colors.js).
const js = vm.createContext({
  document: {addEventListener() {}, getElementById: () => null, querySelector: () => null,
    querySelectorAll: () => [], documentElement: {lang: 'en'}},
  window: {}, navigator: {language: 'en'}, TextEncoder, TextDecoder, Number, Math, String, Array, JSON,
  normalizeMdiIconName: value => String(value || ''),
});
for (const file of ['src/web/admin/core/localization.js', 'src/types/switch/admin.js',
  'src/types/binary_sensor/admin-state.js', 'src/types/climate/admin-preview.js', 'src/types/cover/admin.js',
  'src/web/admin/tiles/icon-colors.js']) vm.runInContext(read(file), js, {filename: file});
for (const [input, media, other] of records) {
  assert.equal(js.normalizeIconColorRecord(input, false, false, true, true, true), media, `JS Media ${JSON.stringify(input)}`);
  assert.equal(js.normalizeIconColorRecord(input, false, false, true, true, false), other, `JS other ${JSON.stringify(input)}`);
}
assert.deepEqual({...js.parseIconColorRecord('v2\n\ncover icon tile=35').cover}, {icon: true, tile: 35});
assert.deepEqual({...js.parseIconColorRecord('v2\nFF0000').cover}, {icon: false, tile: 0});

// Firmware: the same records, and the cover color of synthetic covers.
const esc = text => JSON.stringify(text);
const output = compileAndRun({
  label: 'Media cover color',
  harness: String.raw`
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include "src/tiles/config/tile_icon_colors.h"
#include "src/types/media/cover_color.h"
using namespace tile_icon_colors;
// One output line per record: its line breaks print as '/'.
static const char* norm(const char* in, bool cover) {
  static char out[2][kMaxRecordBytes + 1];
  char* o = out[cover ? 0 : 1];
  normalize(in, o, kMaxRecordBytes + 1, false, false, true, true, cover);
  for (char* p = o; *p; ++p) if (*p == 10) *p = '/';
  return o;
}
// Big-endian RGB565 (the cover decoder's RGB565_SWAPPED bytes).
static void put(std::vector<uint8_t>& px, int w, int x, int y, unsigned rgb) {
  const unsigned v = ((rgb >> 19) & 0x1F) << 11 | ((rgb >> 10) & 0x3F) << 5 | ((rgb >> 3) & 0x1F);
  px[(y * w + x) * 2] = v >> 8;
  px[(y * w + x) * 2 + 1] = v & 0xFF;
}
static void cover(const char* name, int w, int h, unsigned (*paint)(int, int, int, int)) {
  std::vector<uint8_t> px(w * h * 2);
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) put(px, w, x, y, paint(x, y, w, h));
  uint32_t rgb = 0;
  if (media_cover_color::pick(px.data(), w, h, 0, rgb)) std::printf("%s=%06X\n", name, (unsigned)rgb);
  else std::printf("%s=none\n", name);
}
int main() {
  ${records.map(([input], i) => `std::printf("R${i}|%s|", norm(${esc(input)}, true)); std::printf("%s\\n", norm(${esc(input)}, false));`).join('\n  ')}
  const Cover c = cover_of("v2\n\ncover icon tile=35");
  std::printf("cover_of=%d,%u\n", c.icon ? 1 : 0, (unsigned)c.tile);
  const uint8_t red[2] = {0xF8, 0x00};
  std::printf("byte_order=%06X\n", (unsigned)media_cover_color::rgb_of(red));
  // A sunset: orange to red sky with a pale sun.
  cover("sunset", 120, 120, [](int x, int y, int, int h) -> unsigned {
    if ((x - 70) * (x - 70) + (y - 45) * (y - 45) < 18 * 18) return 0xFFD56B;
    return y < h / 2 ? 0xF07A2A : 0xD8452E; });
  // Blue water with a dark night sky.
  cover("ocean", 96, 96, [](int, int y, int, int h) -> unsigned { return y < h / 3 ? 0x101418 : 0x2A6FDB; });
  // Black and white stripes, and grey.
  cover("mono", 64, 64, [](int x, int y, int, int) -> unsigned { return ((x + y) / 8) % 2 ? 0xF0F0F0 : 0x111111; });
  cover("grey", 64, 64, [](int, int, int, int) -> unsigned { return 0x808080; });
  // A tiny colored logo (under 4 % of the cover) on black stays neutral.
  cover("logo", 100, 100, [](int x, int y, int, int) -> unsigned { return x < 15 && y < 15 ? 0x00C853 : 0x000000; });
  // A green forest with a small red dot: the large area wins.
  cover("forest", 80, 80, [](int x, int y, int, int) -> unsigned { return x < 12 && y < 12 ? 0xE53935 : 0x2F8A46; });
  return 0;
}
`,
});
if (output !== null) {
  const lines = output.trim().split('\n');
  records.forEach(([input, media, other], i) => {
    assert.equal(lines[i], `R${i}|${media}|${other}`.replace(/\n/g, '/'), `native ${JSON.stringify(input)}`);
  });
  const value = name => lines.find(line => line.startsWith(name + '=')).slice(name.length + 1);
  assert.equal(value('cover_of'), '1,35');
  assert.equal(value('byte_order'), 'FF0000', 'RGB565_SWAPPED is big-endian');
  const hue = hex => {
    const n = parseInt(hex, 16);
    const [r, g, b] = [(n >> 16) & 255, (n >> 8) & 255, n & 255];
    const max = Math.max(r, g, b), min = Math.min(r, g, b), c = max - min;
    let h = max === r ? 60 * (g - b) / c : max === g ? 120 + 60 * (b - r) / c : 240 + 60 * (r - g) / c;
    return (h + 360) % 360;
  };
  assert.ok(hue(value('sunset')) >= 5 && hue(value('sunset')) <= 30, `sunset is orange-red: ${value('sunset')}`);
  assert.ok(hue(value('ocean')) >= 205 && hue(value('ocean')) <= 225, `ocean is blue: ${value('ocean')}`);
  assert.ok(hue(value('forest')) >= 125 && hue(value('forest')) <= 145, `forest is green: ${value('forest')}`);
  for (const name of ['mono', 'grey', 'logo']) assert.equal(value(name), 'none', `${name} has no cover color`);
}

// Device integration: every cover change reports its color; only Media
// cards with "From cover" follow it, below an active rule.
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
const visible = cppFunctionDefinitions(renderer).find(f => f.name === 'set_media_cover_visible').source;
assert.match(visible, /report_media_cover_color\(widgets, visible\);\n  return changed;/);
const report = cppFunctionDefinitions(renderer).find(f => f.name === 'report_media_cover_color').source;
assert.match(report, /dsc->header\.cf == LV_COLOR_FORMAT_RGB565_SWAPPED/);
assert.match(report, /dsc->data_size >= stride \* \(dsc->header\.h - 1U\) \+ dsc->header\.w \* 2U/, 'bounds checked');
assert.match(report, /tile_icon_source::set_cover_color\(card, known, rgb\);/);

const source = read('src/tiles/runtime/tile_icon_source.cpp');
const refresh = cppFunctionDefinitions(source).find(f => f.name === 'tile_icon_source::refresh_card')?.source ??
  cppFunctionDefinitions(source).find(f => f.name === 'refresh_card').source;
assert.match(refresh, /tile\.type == TILE_MEDIA \? tile_icon_colors::cover_of\(tile\.icon_colors\.c_str\(\)\) : tile_icon_colors::Cover\{\}/,
  'only Media reads "From cover"');
assert.match(refresh, /if \(colored && layer\.icon\) \{\s*tile_icon_disc::force_icon_color\(icon, lv_color_hex\(rgb\)\);\s*\} else if \(cover\.icon\) \{/,
  'an active rule colors the icon first');
assert.match(refresh, /const uint8_t cover_tint = cover\.tile && !choice\.percent \? cover\.tile : 0;/,
  'From icon and an active rule tint first');
assert.match(refresh, /if \(tile\.type == TILE_MEDIA\) set_cover_permissions\(card, cover\.icon && !\(colored && layer\.icon\), cover_tint\);/);
assert.match(refresh, /if \(!cover_tint\) apply_tint_choice\(card, choice\);/);
const apply = cppFunctionDefinitions(source).find(f => f.name === 'apply_cover').source;
assert.match(apply, /const bool known = cover_color\(card, rgb\) && tile_tint::has_hue\(rgb\);/);
assert.match(apply, /if \(known\) set_tile_tint\(card, rgb, tile\);\s*else clear_tile_tint\(card\);/,
  'the tint is tile_tint::background (readable), else the card color');
assert.match(read('src/tiles/config/tile_config.h'), /true, tileTypeRulesUseOwnEntity\(type\), type == TILE_MEDIA\);/);

// Web Admin: Media offers "From cover" as tile color and as icon color.
const html = read('src/web/server/render/tile_icon_colors_html.cpp');
assert.match(html, /append_button\(html, "", "icon-color-mode", "data-mode", "cover", tr\.tile_color_mode_from_cover\);/);
assert.ok(html.includes('_tile_icon_cover" hidden>') && html.includes('_tile_cover_fill" hidden>'));
assert.match(read('src/web/server/render/web_admin_html.cpp'), /\{"cover", tr\.tile_color_mode_from_cover\}/);
const grid = read('src/web/admin/tiles/grid-preview.js');
assert.match(grid, /if \(button\.dataset\.tileColorMode === 'cover'\) button\.classList\.toggle\('hidden', !coverOffered\);/);
assert.match(grid, /const coverOffered = String\(typeValue\) === '15';/);
const i18n = read('src/core/i18n/i18n.cpp');
for (const label of ['"Aus Cover"', '"From cover"', '"De la pochette"']) assert.ok(i18n.includes(label), label);

console.log('Media "From cover": cover color, record on device and in the browser, runtime and editor pass.');
