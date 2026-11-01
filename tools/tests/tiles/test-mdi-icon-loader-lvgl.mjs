// The firmware's MDI icon fonts (src/fonts/mdi_icons_<px>.c) keep only the
// core icon bitmaps and load the others from the icon file through
// mdi_icon_cache.cpp. Real LVGL decodes every glyph of all three sizes through
// that path and must get exactly the bitmaps of the full fonts
// (tools/mdi-fonts), also while the cache is cleared again and again; without
// the file the core icons stay and the others become the unknown-name icon.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {
  SIZES, buildIconFile, fontOutputPath, fontSourcePath, iconFileName, parseMdiFont,
} from '../../generate-icon-files.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const host = await lvglHost(root);
const cc = ['clang', 'gcc'].find((c) => spawnSync(c, ['--version']).status === 0);
if (!host || !cc) {
  console.log('SKIP: the MDI icon loader test needs LVGL and a host compiler');
  process.exit(0);
}

const out = path.join(root, 'build/tests/mdi-icon-loader');
fs.mkdirSync(out, {recursive: true});
for (const px of SIZES) {
  const font = parseMdiFont(fs.readFileSync(fontSourcePath(px), 'utf8'), `mdi_icons_${px}`);
  fs.writeFileSync(path.join(out, iconFileName(px)), buildIconFile(font, px));
}

const cpp = String.raw`
#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "src/tiles/icons/mdi_icon_cache.h"

extern "C" {
extern const lv_font_t mdi_icons_48, mdi_icons_40, mdi_icons_32;  // full fonts
extern const lv_font_t mdi_icons_48_loaded, mdi_icons_40_loaded, mdi_icons_32_loaded;
extern mdi_icon_loader_t mdi_icons_48_loader, mdi_icons_40_loader, mdi_icons_32_loader;
}

static constexpr uint32_t kUnknown = 0xF02D8;  // "hexagon", mdiUnknownIconCodepoint()

struct File { std::vector<uint8_t> bytes; uint32_t reads = 0; bool fail = false; };

static bool read_file(void* ctx, uint32_t offset, uint8_t* out, uint32_t len) {
  File* f = static_cast<File*>(ctx);
  f->reads++;
  if (f->fail || mdi_icon_cache::kHeaderBytes + offset + len > f->bytes.size()) return false;
  memcpy(out, f->bytes.data() + mdi_icon_cache::kHeaderBytes + offset, len);
  return true;
}

static lv_draw_buf_t* g_buf_a;
static lv_draw_buf_t* g_buf_b;

// Decodes one glyph of each font; true when both give the same glyph.
static bool same(const lv_font_t* loaded, uint32_t letter, const lv_font_t* full, uint32_t full_letter) {
  lv_font_glyph_dsc_t a{}, b{};
  const bool fa = lv_font_get_glyph_dsc(loaded, &a, letter, 0);
  const bool fb = lv_font_get_glyph_dsc(full, &b, full_letter, 0);
  if (fa != fb) return false;
  if (!fa) return true;
  if (a.adv_w != b.adv_w || a.box_w != b.box_w || a.box_h != b.box_h || a.ofs_x != b.ofs_x ||
      a.ofs_y != b.ofs_y || a.gid.index != b.gid.index) return false;
  memset(g_buf_a->data, 0, g_buf_a->data_size);
  memset(g_buf_b->data, 0, g_buf_b->data_size);
  // LVGL draws from the returned buffer (lv_draw_sw_letter.c); a cached glyph
  // comes in the glyph cache's own buffer, not copied into the one passed.
  const auto* ra = static_cast<const lv_draw_buf_t*>(lv_font_get_glyph_bitmap(&a, g_buf_a));
  const auto* rb = static_cast<const lv_draw_buf_t*>(lv_font_get_glyph_bitmap(&b, g_buf_b));
  if ((ra == nullptr) != (rb == nullptr)) return false;
  if (!ra) return true;
  const uint32_t stride = lv_draw_buf_width_to_stride(a.box_w, LV_COLOR_FORMAT_A8);
  return memcmp(ra->data, rb->data, stride * a.box_h) == 0;
}

static bool drawn(const lv_font_t* font, uint32_t letter) {
  lv_font_glyph_dsc_t d{};
  if (!lv_font_get_glyph_dsc(font, &d, letter, 0)) return false;
  return lv_font_get_glyph_bitmap(&d, g_buf_a) != nullptr;
}

static bool is_core(const mdi_icon_loader_t* loader, uint32_t gid) {
  const mdi_icon_font_data_t& d = *loader->data;
  for (uint32_t i = 0; i < d.core_count; i++) if (d.core_ids[i] == gid) return true;
  return false;
}

static uint32_t gid_of(const lv_font_t* font, uint32_t letter) {
  lv_font_glyph_dsc_t d{};
  return lv_font_get_glyph_dsc(font, &d, letter, 0) ? d.gid.index : 0;
}

static int run(const char* file_path, const lv_font_t* full, const lv_font_t* loaded, mdi_icon_loader_t* loader,
               bool first) {
  const mdi_icon_font_data_t& data = *loader->data;
  File file;
  FILE* fp = fopen(file_path, "rb");
  if (!fp) return printf("no file %s\n", file_path), 1;
  fseek(fp, 0, SEEK_END);
  file.bytes.resize(ftell(fp));
  fseek(fp, 0, SEEK_SET);
  if (fread(file.bytes.data(), 1, file.bytes.size(), fp) != file.bytes.size()) return 2;
  fclose(fp);

  // Before attach: core icons drawn, the others not (and no unknown icon yet).
  uint32_t core_letter = 0, other_letter = 0;
  for (uint32_t cp = 0xF0000; cp < 0xF2000 && (!core_letter || !other_letter); cp++) {
    const uint32_t gid = gid_of(full, cp);
    if (!gid) continue;
    if (is_core(loader, gid)) { if (!core_letter && cp != kUnknown) core_letter = cp; }
    else if (!other_letter) other_letter = cp;
  }
  if (!same(loaded, core_letter, full, core_letter)) return printf("core before attach\n"), 3;
  if (drawn(loaded, other_letter)) return printf("non-core drawn before attach\n"), 4;

  // Attached without a file: core icons stay, the others become the unknown icon.
  std::vector<uint8_t> block(mdi_icon_cache::block_bytes(data));
  if (!mdi_icon_cache::attach(loader, block.data(), block.size(), kUnknown)) return 5;
  uint32_t core = 0, substituted = 0;
  for (uint32_t cp = 0xF0000; cp < 0xF2000; cp++) {
    const uint32_t gid = gid_of(full, cp);
    if (!gid) continue;
    if (is_core(loader, gid)) {
      if (!same(loaded, cp, full, cp)) return printf("core %X without file\n", cp), 6;
      core++;
    } else {
      if (!same(loaded, cp, full, kUnknown)) return printf("unknown for %X\n", cp), 7;
      substituted++;
    }
  }

  // With the file: every glyph byte for byte, through repeated cache clears.
  const mdi_icon_cache::Source source{read_file, &file};
  mdi_icon_cache::set_source(loader, &source);
  uint32_t glyphs = 0;
  for (int pass = 0; pass < 2; pass++) {
    for (uint32_t cp = 0xF0000; cp < 0xF2000; cp++) {
      if (!gid_of(full, cp)) continue;
      if (!same(loaded, cp, full, cp)) return printf("glyph %X pass %d\n", cp, pass), 8;
      if (pass == 0) glyphs++;
    }
  }
  const mdi_icon_cache::Stats all = mdi_icon_cache::stats(loader);
  if (all.clears < 2 || all.failures) return printf("clears %u failures %u\n", all.clears, all.failures), 9;

  // Cached glyphs are not read again.
  mdi_icon_cache::set_source(loader, &source);
  const uint32_t reads_before = file.reads;
  uint32_t from_file = 0;
  for (uint32_t cp = other_letter; cp < other_letter + 40; cp++) {
    const uint32_t gid = gid_of(full, cp);
    if (gid && !is_core(loader, gid)) from_file++;
  }
  for (int pass = 0; pass < 3; pass++) {
    for (uint32_t cp = other_letter; cp < other_letter + 40; cp++) same(loaded, cp, full, cp);
  }
  const uint32_t cached_reads = file.reads - reads_before;

  // A failing read draws nothing and is counted.
  mdi_icon_cache::set_source(loader, &source);
  file.fail = true;
  if (drawn(loaded, other_letter)) return printf("drawn after a failed read\n"), 10;
  file.fail = false;
  const uint32_t failures = mdi_icon_cache::stats(loader).failures;

  // Header checks.
  using mdi_icon_cache::HeaderCheck;
  const uint8_t* h = file.bytes.data();
  const uint32_t size = static_cast<uint32_t>(file.bytes.size());
  HeaderCheck ok = mdi_icon_cache::check_header(h, 32, size, data);
  std::vector<uint8_t> bad(h, h + 32);
  bad[9] ^= 1;
  HeaderCheck damaged = mdi_icon_cache::check_header(bad.data(), 32, size, data);
  HeaderCheck short_file = mdi_icon_cache::check_header(h, 32, size - 1, data);
  HeaderCheck magic = mdi_icon_cache::check_header(reinterpret_cast<const uint8_t*>("HTIX0000000000000000000000000000"), 32, size, data);
  const uint32_t crc = mdi_icon_cache::crc32(0, h + 32, size - 32);
  const bool crc_ok = crc == mdi_icon_cache::header_bitmap_crc(h);

  printf("%s{\"px\":%u,\"glyphs\":%u,\"core\":%u,\"substituted\":%u,\"loads\":%u,\"clears\":%u,"
         "\"capacityKB\":%u,\"cachedReads\":%u,\"fromFile\":%u,\"failures\":%u,\"ok\":%d,\"damaged\":%d,\"short\":%d,"
         "\"magic\":%d,\"crc\":%d}",
         first ? "" : ",", data.px, glyphs, core, substituted, all.loads, all.clears,
         all.bitmap_capacity / 1024, cached_reads, from_file, failures, ok == HeaderCheck::Ok,
         damaged == HeaderCheck::HeaderCrc, short_file == HeaderCheck::Length, magic == HeaderCheck::Magic, crc_ok);
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 4) return 20;
  lv_init();
  g_buf_a = lv_draw_buf_create(64, 64, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
  g_buf_b = lv_draw_buf_create(64, 64, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
  if (mdi_icon_cache::crc32(0, reinterpret_cast<const uint8_t*>("123456789"), 9) != 0xCBF43926u) return 21;
  printf("[");
  int r = run(argv[1], &mdi_icons_48, &mdi_icons_48_loaded, &mdi_icons_48_loader, true);
  if (!r) r = run(argv[2], &mdi_icons_40, &mdi_icons_40_loaded, &mdi_icons_40_loader, false);
  if (!r) r = run(argv[3], &mdi_icons_32, &mdi_icons_32_loaded, &mdi_icons_32_loader, false);
  printf("]\n");
  return r;
}
`;

const run = (cmd, args) => {
  const result = spawnSync(cmd, args, {encoding: 'utf8', maxBuffer: 64 * 1024 * 1024});
  assert.equal(result.status, 0, `${cmd} ${args.join(' ')}\n${result.stdout}${result.stderr}`);
  return result.stdout;
};
const objects = [];
for (const px of SIZES) {
  const obj = path.join(out, `mdi_icons_${px}_loaded.o`);
  run(cc, [...host.flags, `-Dmdi_icons_${px}=mdi_icons_${px}_loaded`, '-O1', '-w', '-c', fontOutputPath(px), '-o', obj]);
  objects.push(obj);
}
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
run(host.cxx, [...host.flags, '-std=c++17', '-O1', source, path.join(root, 'src/tiles/icons/mdi_icon_cache.cpp'),
  ...objects, host.archive, '-o', binary]);
const result = spawnSync(binary, SIZES.map((px) => path.join(out, iconFileName(px))), {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
const sizes = JSON.parse(result.stdout.trim().split('\n').at(-1));
for (const s of sizes) {
  assert.equal(s.glyphs, 7447, `${s.px}: every glyph decoded`);
  assert.equal(s.core + s.substituted, 7447, `${s.px}: core plus unknown icons cover the font without the file`);
  assert.ok(s.core >= 270, `${s.px}: core icons drawn without the file (${s.core})`);
  assert.ok(s.clears >= 2, `${s.px}: the cache refilled after clears (${s.clears})`);
  assert.ok(s.fromFile > 30, `${s.px}: most of the 40 icons come from the file (${s.fromFile})`);
  assert.equal(s.cachedReads, s.fromFile, `${s.px}: each icon read once over three passes`);
  assert.equal(s.failures, 1, `${s.px}: a failed read is counted`);
  for (const key of ['ok', 'damaged', 'short', 'magic', 'crc']) assert.equal(s[key], 1, `${s.px}: header ${key}`);
}
console.log(`PASS: icon loader decodes every MDI glyph like the full fonts ${JSON.stringify(sizes)}`);
