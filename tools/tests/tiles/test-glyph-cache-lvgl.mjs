// LVGL unpacked a compressed tile icon again for every draw band it crossed
// (V2 b590 FrameProbe: the 48 px icon fonts cost ~0.3 ms per piece, 25 ms of
// a 137 ms Home frame; user 2026-10-28: make every tile cheaper). The six tile
// icon fonts now unpack each glyph once into a bounded PSRAM cache
// (src/fonts/glyph_cache.c) and draw it from there for every later piece. The
// text fonts widened every 4-bit glyph to 8 bits per piece the same way (V2
// b598: 20 px titles 130 us per piece) and use the cache too; a kept glyph is
// handed out in the cache's own buffer, without a copy. Real LVGL must get
// exactly the bytes it unpacks itself, in rows of its own stride, the second
// and third use must come from the cache, and a new icon file empties it.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const read = (p) => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');

for (const px of [32, 40, 48]) {
  for (const name of [`mdi_icons_${px}`, `weather_icons_${px}`]) {
    const font = read(`src/fonts/${name}.c`);
    assert.ok(font.includes('#include "src/fonts/glyph_cache.h"'), `${name} includes the glyph cache`);
    assert.match(font, /\n {4}\.get_glyph_bitmap = glyph_cache_bitmap,/, `${name} unpacks through the cache`);
    assert.doesNotMatch(font, /\.get_glyph_bitmap = lv_font_get_bitmap_fmt_txt/, `${name} no longer unpacks per band`);
  }
}
// Regenerating keeps it.
const mdiGenerator = read('tools/generate-icon-files.mjs');
assert.ok(mdiGenerator.includes('#include "src/fonts/glyph_cache.h"') &&
  mdiGenerator.includes('    .get_glyph_bitmap = glyph_cache_bitmap,'), 'the MDI generator writes the cache');
const weatherGenerator = read('tools/generate-weather-icon-fonts.mjs');
assert.ok(weatherGenerator.includes('#include "src/fonts/glyph_cache.h"') &&
  weatherGenerator.includes("'.get_glyph_bitmap = glyph_cache_bitmap,"), 'the weather generator writes the cache');

// Every text font: plain 4-bit (ui_font_12..48, cyrillic, symbols) and the
// compressed clock sizes (ui_font_56..96).
const textFonts = fs.readdirSync(path.join(root, 'src/fonts')).filter(f => /^ui_(font|symbols)_.*\.c$/.test(f));
assert.ok(textFonts.length >= 21, `all text fonts listed (${textFonts.length})`);
for (const file of textFonts) {
  const font = read(`src/fonts/${file}`);
  assert.ok(font.includes('#include "src/fonts/glyph_cache.h"'), `${file} includes the glyph cache`);
  assert.ok(font.includes('.get_glyph_bitmap = glyph_cache_bitmap,'), `${file} draws through the cache`);
  assert.ok(!font.includes('.get_glyph_bitmap = lv_font_get_bitmap_fmt_txt'), `${file} no longer widens per piece`);
}

const cache = read('src/fonts/glyph_cache.c');
assert.ok(cache.includes('heap_caps_malloc(sizeof(glyph_cache_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)'),
  'the cache lives in PSRAM, never in the internal draw memory');
assert.ok(cache.includes('#if !defined(ESP_PLATFORM) || defined(CONFIG_IDF_TARGET_ESP32P4)') &&
  cache.includes('#define GLYPH_CACHE_BYTES (512u * 1024u)') && cache.includes('#define GLYPH_CACHE_ENTRIES 1024u') &&
  cache.includes('#define GLYPH_CACHE_BYTES (256u * 1024u)') && cache.includes('#define GLYPH_CACHE_ENTRIES 512u'),
  'the cache is bounded, half on the S3');
assert.ok(cache.includes('s_hit.data = s_cache->bytes + e->offset;') && cache.includes('return &s_hit;') &&
  !/memcpy\(draw_buf->data/.test(cache), 'a kept glyph is handed out without a copy');
assert.ok(cache.includes('if (g_dsc->req_raw_bitmap || !draw_buf || !g_dsc->gid.index || !ready()) {'),
  'raw bitmap requests and a missing cache go straight to LVGL');
assert.match(read('src/tiles/icons/mdi_icon_cache.cpp'),
  /void set_source\(mdi_icon_loader_t\* loader, const Source\* source\) \{[\s\S]*?glyph_cache_clear\(\);/,
  'a new icon file empties the cache');
assert.ok(read('tools/lib/lvgl-host.mjs').includes("path.join(root, 'src/fonts/glyph_cache.c')"),
  'the host LVGL build links the cache with the fonts');

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the glyph cache LVGL run needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}

const cpp = String.raw`
#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include "src/fonts/glyph_cache.h"

extern "C" {
extern const lv_font_t weather_icons_48, weather_icons_40, weather_icons_32;
extern const lv_font_t ui_font_20, ui_font_40, ui_font_72;
}

static lv_draw_buf_t* g_a;
static lv_draw_buf_t* g_b;

// One glyph through the font (the cache) and straight through LVGL:
// 1 same bytes, 0 not a glyph of this font, -1 different.
static int check(const lv_font_t* font, uint32_t letter) {
  lv_font_glyph_dsc_t g{};
  if (!lv_font_get_glyph_dsc(font, &g, letter, 0) || g.resolved_font != font || !g.box_w || !g.box_h) return 0;
  lv_font_glyph_dsc_t direct = g;
  // As lv_draw_label.c does before each letter.
  lv_draw_buf_t* a = lv_draw_buf_reshape(g_a, LV_COLOR_FORMAT_UNKNOWN, g.box_w, g.box_h, LV_STRIDE_AUTO);
  lv_draw_buf_t* b = lv_draw_buf_reshape(g_b, LV_COLOR_FORMAT_UNKNOWN, g.box_w, g.box_h, LV_STRIDE_AUTO);
  if (!a || !b) return -1;
  memset(a->data, 0, a->data_size);
  memset(b->data, 0, b->data_size);
  const auto* ra = static_cast<const lv_draw_buf_t*>(lv_font_get_glyph_bitmap(&g, a));
  const auto* rb = static_cast<const lv_draw_buf_t*>(lv_font_get_bitmap_fmt_txt(&direct, b));
  if (!ra || rb != b) return -1;
  const uint32_t stride = lv_draw_buf_width_to_stride(g.box_w, LV_COLOR_FORMAT_A8);
  if (ra->header.stride != stride || rb->header.stride != stride) return -1;
  return memcmp(ra->data, rb->data, stride * g.box_h) == 0 ? 1 : -1;
}

static int pass(unsigned* glyphs) {
  const lv_font_t* icons[] = {&weather_icons_48, &weather_icons_40, &weather_icons_32};
  const lv_font_t* texts[] = {&ui_font_20, &ui_font_40, &ui_font_72};
  unsigned n = 0;
  for (const lv_font_t* font : icons) {
    for (uint32_t cp = 0xE000; cp < 0xE040; cp++) {
      const int r = check(font, cp);
      if (r < 0) return printf("glyph %X differs\n", cp), 1;
      n += r;
    }
  }
  for (const lv_font_t* font : texts) {
    for (uint32_t cp = 0x21; cp < 0x7F; cp++) {
      const int r = check(font, cp);
      if (r < 0) return printf("letter %X of a text font differs\n", cp), 1;
      n += r;
    }
  }
  *glyphs = n;
  return 0;
}

int main() {
  lv_init();
  g_a = lv_draw_buf_create(96, 128, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
  g_b = lv_draw_buf_create(96, 128, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
  unsigned glyphs = 0;
  for (int i = 0; i < 3; i++) if (pass(&glyphs)) return 1;
  const glyph_cache_stats_t three = glyph_cache_stats();
  glyph_cache_clear();
  const glyph_cache_stats_t cleared = glyph_cache_stats();
  if (pass(&glyphs)) return 2;
  const glyph_cache_stats_t after = glyph_cache_stats();
  printf("{\"glyphs\":%u,\"misses\":%u,\"hits\":%u,\"clears\":%u,\"kept\":%u,\"keptAfterClear\":%u,"
         "\"missesAfterClear\":%u,\"off\":%u}\n",
         glyphs, three.misses, three.hits, three.clears, three.glyphs, cleared.glyphs,
         after.misses - three.misses, three.off);
  return 0;
}
`;

const out = path.join(root, 'build/tests/glyph-cache');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-O1', source, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
const run = JSON.parse(result.stdout.trim().split('\n').at(-1));
assert.ok(run.glyphs >= 30 + 3 * 90, `the weather layers and three text sizes were unpacked (${run.glyphs})`);
assert.equal(run.off, 0, 'the cache memory was allocated');
assert.equal(run.misses, run.glyphs, 'each glyph unpacked once over three passes');
assert.equal(run.hits, 2 * run.glyphs, 'the second and third use come from the cache');
assert.equal(run.clears, 0, 'all of them fit');
assert.equal(run.kept, run.glyphs);
assert.equal(run.keptAfterClear, 0, 'a clear drops every glyph');
assert.equal(run.missesAfterClear, run.glyphs, 'and they are unpacked again');
console.log(`PASS: icon and text glyphs are unpacked once and drawn byte for byte from the cache ${JSON.stringify(run)}`);
