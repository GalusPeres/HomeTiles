#!/usr/bin/env node
// Maps look-alike characters onto glyphs the panel fonts already carry:
//   node tools/font-aliases.mjs           adds missing aliases to src/fonts/ui_font_*.c
//   node tools/font-aliases.mjs --check   fails when a font lacks one
// Home Assistant writes micro units with the Greek small letter mu U+03BC
// ("μg/m³", "μS/cm", "μs"; issue #115), while the fonts carry Latin-1 and
// with it the micro sign U+00B5, the same letter in Inter. Each alias is one
// more one-codepoint cmap entry pointing at the existing glyph, so it costs a
// few bytes per font, keeps the glyph's kerning, and needs no text rewriting
// anywhere in the firmware. lv_font_conv does not write these entries:
// run this again after regenerating a font (test-font-aliases checks it).
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const FONT_DIR = path.join(root, 'src/fonts');

// [alias, carried glyph]
export const ALIASES = [[0x03BC, 0x00B5]];

// The cmap entries of a generated LVGL font, in table order.
export function readCmaps(source) {
  const table = /static const lv_font_fmt_txt_cmap_t cmaps\[\] =\s*\{([\s\S]*?)\n\};/.exec(source);
  if (!table) return null;
  const lists = new Map([...source.matchAll(/static const uint16_t (unicode_list_\d+)\[\] = \{([^}]*)\}/g)]
    .map((m) => [m[1], m[2].split(',').map((v) => v.trim()).filter(Boolean).map((v) => parseInt(v, 16))]));
  return [...table[1].matchAll(/\{([^{}]*)\}/g)].map((m) => {
    const field = (name) => new RegExp(`\\.${name} = ([^,\\s]+)`).exec(m[1])?.[1];
    return {
      rangeStart: Number(field('range_start')),
      rangeLength: Number(field('range_length')),
      glyphIdStart: Number(field('glyph_id_start')),
      unicodeList: field('unicode_list') === 'NULL' ? null : lists.get(field('unicode_list')),
      glyphIdOfsList: field('glyph_id_ofs_list'),
      type: field('type'),
    };
  });
}

// The glyph id LVGL's get_glyph_dsc_id() finds: the first cmap whose range
// holds the letter (0 = not in the font).
export function glyphId(cmaps, letter) {
  for (const cmap of cmaps) {
    const rcp = letter - cmap.rangeStart;
    if (rcp < 0 || rcp >= cmap.rangeLength) continue;
    if (cmap.type === 'LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY') return cmap.glyphIdStart + rcp;
    if (cmap.type === 'LV_FONT_FMT_TXT_CMAP_SPARSE_TINY' && cmap.glyphIdOfsList === 'NULL') {
      const index = cmap.unicodeList.indexOf(rcp);
      if (index >= 0) return cmap.glyphIdStart + index;
      continue;
    }
    throw new Error(`cmap type ${cmap.type} is not handled`);
  }
  return 0;
}

export function uiFontFiles() {
  return fs.readdirSync(FONT_DIR).filter((name) => /^ui_font_.*\.c$/.test(name)).sort()
    .map((name) => path.join(FONT_DIR, name));
}

// Adds the missing aliases of one font source; returns the new source.
export function withAliases(source) {
  let cmaps = readCmaps(source);
  if (!cmaps) return source;
  for (const [alias, carried] of ALIASES) {
    const target = glyphId(cmaps, carried);
    if (!target || glyphId(cmaps, alias)) continue;
    const entry = `,\n    {\n        .range_start = ${alias}, .range_length = 1, .glyph_id_start = ${target},\n` +
      '        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY\n    }';
    source = source.replace(/(static const lv_font_fmt_txt_cmap_t cmaps\[\] =\s*\{[\s\S]*?\n    \})(\n\};)/, `$1${entry}$2`);
    const count = readCmaps(source).length;
    source = source.replace(/\.cmap_num = \d+,/, `.cmap_num = ${count},`);
    cmaps = readCmaps(source);
  }
  return source;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const check = process.argv.includes('--check');
  const stale = [];
  for (const file of uiFontFiles()) {
    const source = fs.readFileSync(file, 'utf8');
    const next = withAliases(source);
    if (next === source) continue;
    stale.push(path.relative(root, file));
    if (!check) fs.writeFileSync(file, next);
  }
  if (check && stale.length) {
    console.error(`Fonts without their aliases (run node tools/font-aliases.mjs): ${stale.join(', ')}`);
    process.exit(1);
  }
  console.log(check ? 'Font aliases: every UI font carries them' : `Font aliases added to ${stale.length} font(s)`);
}
