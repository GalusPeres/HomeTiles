#!/usr/bin/env node
// Builds the MDI icon files that move the icon bitmaps out of the firmware
// image (the 48 px icons alone take about 2 MB of the 6.5 MB app slot).
//
// Input: the full lv_font_conv fonts tools/mdi-fonts/mdi_icons_<48|40|32>.c
// (one per layout, generate-mdi-fonts.ps1). Output:
// - build/icons/hometiles-icons-<px>.bin: the font's glyph bitmaps byte for
//   byte behind a small header.
// - src/fonts/mdi_icons_<px>.c: the compiled font, with the glyph descriptors
//   and character map of the full font but only the bitmaps of the core
//   icons; LVGL loads the others through mdi_icon_loader.h.
// A bitmap's place in the file is its descriptor's bitmap_index; the header
// names the descriptor table it belongs to (CRC32) so a file of another font
// generation is never used.
//
// File layout (little endian, 32 byte header):
//   0  "HTIF"                    magic
//   4  u16 format (1)
//   6  u16 pixel size (48, 40, 32)
//   8  u32 glyph count, reserved glyph 0 included
//  12  u32 bitmap bytes
//  16  u32 CRC32 of the glyph descriptor table (tableBytes)
//  20  u32 CRC32 of the bitmap bytes
//  24  u32 reserved (0)
//  28  u32 CRC32 of bytes 0..27
//  32  bitmap bytes
//
// The icons the firmware must show without the file (its own icon names and
// the Bridge's default icons) are listed in tools/icons/core-icons.txt; they
// stay built in. `--update-core [--bridge <path>]` rewrites that list from the
// firmware sources and the Bridge integration.
//
// Usage:
//   node tools/generate-icon-files.mjs [--out <dir>] [--files-only]
//     --files-only: only the icon files, for release assets
//   node tools/generate-icon-files.mjs --update-core --ha <tag> [--bridge <path>]
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

export const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
export const SIZES = [48, 40, 32];
export const FORMAT = 1;
export const HEADER_BYTES = 32;
export const MAGIC = 'HTIF';
export const CORE_LIST = 'tools/icons/core-icons.txt';

export const fontSourcePath = (px) => path.join(root, 'tools/mdi-fonts', `mdi_icons_${px}.c`);
export const fontOutputPath = (px) => path.join(root, 'src/fonts', `mdi_icons_${px}.c`);
export const iconFileName = (px) => `hometiles-icons-${px}.bin`;

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();

// The zlib CRC32 (also the ESP ROM's esp_rom_crc32_le(0, ...)).
export function crc32(bytes) {
  let crc = 0xFFFFFFFF;
  for (let i = 0; i < bytes.length; i++) crc = CRC_TABLE[(crc ^ bytes[i]) & 0xFF] ^ (crc >>> 8);
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

function section(source, startPattern, name) {
  const start = source.search(startPattern);
  if (start < 0) throw new Error(`${name}: section not found`);
  const end = source.indexOf('};', start);
  if (end < 0) throw new Error(`${name}: section has no end`);
  return source.slice(start, end);
}

// Reads an lv_font_conv C font: glyph bitmaps, glyph descriptors, character
// maps and the font descriptor fields the files depend on.
export function parseMdiFont(source, name = 'font') {
  source = source.replace(/\r\n/g, '\n');
  const bitmapText = section(source, /glyph_bitmap\[\] = \{/, name);
  const bytes = [];
  for (const m of bitmapText.matchAll(/0x([0-9a-fA-F]{1,2})\b/g)) bytes.push(parseInt(m[1], 16));
  const bitmap = Uint8Array.from(bytes);

  const glyphs = [...section(source, /glyph_dsc\[\] = \{/, name).matchAll(
    /\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}/g,
  )].map((m) => ({
    bitmapIndex: +m[1], advW: +m[2], boxW: +m[3], boxH: +m[4], ofsX: +m[5], ofsY: +m[6],
  }));
  const cmaps = [...source.matchAll(
    /\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),\s*\.unicode_list = (\w+), \.glyph_id_ofs_list = (\w+), \.list_length = (\d+), \.type = (\w+)/g,
  )].map((m) => ({start: +m[1], length: +m[2], glyphStart: +m[3], type: m[7]}));
  for (const cmap of cmaps) {
    if (cmap.type !== 'LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY') {
      throw new Error(`${name}: unexpected character map ${cmap.type}`);
    }
  }
  const field = (pattern) => {
    const m = source.match(pattern);
    if (!m) throw new Error(`${name}: ${pattern} not found`);
    return Number(m[1]);
  };
  const font = {
    glyphs, cmaps, bitmap,
    bpp: field(/\.bpp = (\d+),/),
    bitmapFormat: field(/\.bitmap_format = (\d+),/),
    lineHeight: field(/\.line_height = (\d+),/),
    baseLine: field(/\.base_line = (-?\d+),/),
  };
  if (glyphs.length < 2 || glyphs[0].boxW !== 0) throw new Error(`${name}: no glyph descriptors`);
  for (let id = 1; id < glyphs.length; id++) {
    if (glyphs[id].bitmapIndex < glyphs[id - 1].bitmapIndex || glyphs[id].bitmapIndex > bitmap.length) {
      throw new Error(`${name}: glyph ${id} bitmap index out of order`);
    }
  }
  return font;
}

export function glyphIdOf(font, codepoint) {
  for (const cmap of font.cmaps) {
    const offset = codepoint - cmap.start;
    if (offset >= 0 && offset < cmap.length) return cmap.glyphStart + offset;
  }
  return 0;
}

// A glyph's bitmap ends where the next one starts; the last one at the end.
export function glyphBytes(font, id) {
  const end = id + 1 < font.glyphs.length ? font.glyphs[id + 1].bitmapIndex : font.bitmap.length;
  return font.bitmap.subarray(font.glyphs[id].bitmapIndex, end);
}

// The descriptor table as the firmware compiles it with LV_FONT_FMT_TXT_LARGE:
// u32 bitmap_index, u32 adv_w, u16 box_w, u16 box_h, i16 ofs_x, i16 ofs_y.
export function tableBytes(font) {
  const out = Buffer.alloc(font.glyphs.length * 16);
  font.glyphs.forEach((g, i) => {
    const o = i * 16;
    out.writeUInt32LE(g.bitmapIndex, o);
    out.writeUInt32LE(g.advW, o + 4);
    out.writeUInt16LE(g.boxW, o + 8);
    out.writeUInt16LE(g.boxH, o + 10);
    out.writeInt16LE(g.ofsX, o + 12);
    out.writeInt16LE(g.ofsY, o + 14);
  });
  return out;
}

export function buildIconFile(font, px) {
  const header = Buffer.alloc(HEADER_BYTES);
  header.write(MAGIC, 0, 'latin1');
  header.writeUInt16LE(FORMAT, 4);
  header.writeUInt16LE(px, 6);
  header.writeUInt32LE(font.glyphs.length, 8);
  header.writeUInt32LE(font.bitmap.length, 12);
  header.writeUInt32LE(crc32(tableBytes(font)), 16);
  header.writeUInt32LE(crc32(font.bitmap), 20);
  header.writeUInt32LE(0, 24);
  header.writeUInt32LE(crc32(header.subarray(0, 28)), 28);
  return Buffer.concat([header, Buffer.from(font.bitmap)]);
}

// Reads and checks an icon file the way the firmware does.
export function readIconFile(file) {
  if (file.length < HEADER_BYTES) throw new Error('icon file: shorter than its header');
  if (file.toString('latin1', 0, 4) !== MAGIC) throw new Error('icon file: wrong magic');
  if (crc32(file.subarray(0, 28)) !== file.readUInt32LE(28)) throw new Error('icon file: header CRC mismatch');
  const header = {
    format: file.readUInt16LE(4),
    px: file.readUInt16LE(6),
    glyphs: file.readUInt32LE(8),
    bitmapBytes: file.readUInt32LE(12),
    tableCrc: file.readUInt32LE(16),
    bitmapCrc: file.readUInt32LE(20),
  };
  if (header.format !== FORMAT) throw new Error(`icon file: format ${header.format}`);
  if (file.length !== HEADER_BYTES + header.bitmapBytes) throw new Error('icon file: wrong length');
  const bitmap = file.subarray(HEADER_BYTES);
  if (crc32(bitmap) !== header.bitmapCrc) throw new Error('icon file: bitmap CRC mismatch');
  return {header, bitmap};
}

// Ascending glyph ids of the core icons in a font.
export function coreGlyphIds(font, coreNames = readCoreList(), names = iconNameTable()) {
  const ids = coreNames.map((name) => {
    const id = glyphIdOf(font, names.get(name));
    if (!id) throw new Error(`core icon ${name} is not in the font`);
    return id;
  });
  return [...new Set(ids)].sort((a, b) => a - b);
}

const hexBytes = (bytes) => {
  const lines = [];
  for (let i = 0; i < bytes.length; i += 16) {
    lines.push(`    ${[...bytes.subarray(i, i + 16)].map((b) => `0x${b.toString(16)}`).join(', ')}`);
  }
  return lines.join(',\n');
};

const numberList = (values, perLine) => {
  const lines = [];
  for (let i = 0; i < values.length; i += perLine) lines.push(`    ${values.slice(i, i + perLine).join(', ')}`);
  return lines.join(',\n');
};

function block(source, startPattern, name) {
  const start = source.search(startPattern);
  if (start < 0) throw new Error(`${name}: ${startPattern} not found`);
  const end = source.indexOf('};', start);
  if (end < 0) throw new Error(`${name}: ${startPattern} has no end`);
  return source.slice(start, end + 2);
}

// The compiled font: the full font's guard, glyph descriptors and character
// map; core bitmaps; the loader that fetches the other bitmaps.
export function compiledFontSource(source, font, px, coreIds) {
  source = source.replace(/\r\n/g, '\n');
  const name = `mdi_icons_${px}`;
  const macro = `MDI_ICONS_${px}`;
  const guard = source.match(new RegExp(`#ifndef ${macro}\\n[\\s\\S]*?\\n#endif\\n#endif`));
  if (!guard) throw new Error(`${name}: guard block not found`);
  const cmapNum = source.match(/\.cmap_num = (\d+),/);
  if (!cmapNum) throw new Error(`${name}: cmap_num not found`);
  const offsets = [0];
  const parts = coreIds.map((id) => {
    const bytes = glyphBytes(font, id);
    offsets.push(offsets[offsets.length - 1] + bytes.length);
    return bytes;
  });
  const coreBitmap = Buffer.concat(parts.map((bytes) => Buffer.from(bytes)));
  return `/*******************************************************************************
 * MDI icons, ${px} px: glyph descriptors, character map and the bitmaps of the
 * ${coreIds.length} built-in core icons (tools/icons/core-icons.txt). LVGL loads the other
 * bitmaps through ${name}_loader (src/tiles/icons/mdi_icon_cache.cpp) from
 * the icon file ${iconFileName(px)}.
 * Generated by tools/generate-icon-files.mjs from tools/mdi-fonts/${name}.c.
 * Do not edit by hand.
 ******************************************************************************/

#include "lvgl.h"

#include "src/devices/device_select.h"
#include "src/fonts/glyph_cache.h"
#include "src/tiles/icons/mdi_icon_loader.h"

${guard[0]}

#if ${macro}

/*Glyph ids of the core icons, ascending*/
static const uint16_t core_ids[] = {
${numberList(coreIds, 12)}
};

/*Start of each core icon's bitmap in core_bitmap, and the end*/
static const uint32_t core_offsets[] = {
${numberList(offsets, 10)}
};

static LV_ATTRIBUTE_LARGE_CONST const uint8_t core_bitmap[] = {
${hexBytes(coreBitmap)}
};

/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

${block(source, /static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc\[\] = \{/, name)}

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

${block(source, /static const lv_font_fmt_txt_cmap_t cmaps\[\] =/, name)}

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

const mdi_icon_font_data_t ${name}_data = {
    .px = ${px},
    .glyph_count = ${font.glyphs.length},
    .bitmap_bytes = ${font.bitmap.length},
    .table_crc = 0x${crc32(tableBytes(font)).toString(16).padStart(8, '0')},
    .glyph_dsc = glyph_dsc,
    .core_count = ${coreIds.length},
    .core_ids = core_ids,
    .core_offsets = core_offsets,
    .core_bitmap = core_bitmap,
};

mdi_icon_loader_t ${name}_loader = {
    .base = {.get_glyph_bitmap_cb = mdi_icon_loader_bitmap},
    .data = &${name}_data,
    .cache = NULL,
};

static const lv_font_fmt_txt_dsc_t font_dsc = {
    .glyph_bitmap = (const uint8_t *)&${name}_loader,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = ${cmapNum[1]},
    .bpp = ${font.bpp},
    .kern_classes = 0,
    .bitmap_format = ${font.bitmapFormat},
    .are_glyphs_dynamic_loaded = true,
};

/*-----------------
 *  PUBLIC FONT
 *----------------*/

const lv_font_t ${name} = {
    .get_glyph_dsc = mdi_icon_loader_glyph_dsc,
    .get_glyph_bitmap = glyph_cache_bitmap,
    .line_height = ${font.lineHeight},
    .base_line = ${font.baseLine},
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = 0,
    .underline_thickness = 0,
    .static_bitmap = 0,
    .dsc = &font_dsc,
    .fallback = NULL,
    .user_data = NULL,
};

#endif /*#if ${macro}*/
`;
}

// Icon name -> codepoint, from the firmware's own name table.
export function iconNameTable() {
  const table = fs.readFileSync(path.join(root, 'src/tiles/icons/mdi_icons.cpp'), 'utf8');
  return new Map([...table.matchAll(/\{"([a-z0-9-]+)", 0x([0-9A-Fa-f]+)\}/g)]
    .map((m) => [m[1], parseInt(m[2], 16)]));
}

function sourceFiles(directory, extensions, skip) {
  return fs.readdirSync(directory, {withFileTypes: true}).flatMap((entry) => {
    const full = path.join(directory, entry.name);
    const relative = path.relative(root, full).replaceAll('\\', '/');
    if (skip.some((s) => relative === s || relative.startsWith(`${s}/`))) return [];
    if (entry.isDirectory()) return sourceFiles(full, extensions, skip);
    return extensions.includes(path.extname(entry.name)) ? [full] : [];
  });
}

// Every string literal of the firmware sources (C++ and the Web Admin's
// scripts, whose type defaults end up as tile icons) that names an MDI icon.
// Names are passed around as plain strings, so a literal that happens to be
// an icon name ("home", "close") counts too; a few extra built-in icons cost
// a few hundred bytes each. A format string such as "wifi-strength-%u%s"
// counts every icon it can produce (a number for %u/%d, any name part for %s).
export function firmwareIconNames(names = iconNameTable()) {
  const files = [
    path.join(root, 'HomeTiles.ino'),
    ...sourceFiles(path.join(root, 'src'), ['.cpp', '.h', '.c', '.ino', '.js'],
      ['src/fonts', 'src/web/generated']),
  ];
  const found = new Set();
  const patterns = new Set();
  for (const file of files) {
    // The name table of mdi_icons.cpp lists every icon; only its code counts.
    const text = fs.readFileSync(file, 'utf8')
      .replace(/static const IconMapping iconMap\[\] PROGMEM = \{[\s\S]*?\n\};/, '');
    for (const m of text.matchAll(/["'`](?:mdi:)?([a-z0-9]+(?:-[a-z0-9]+)*)["'`]/g)) {
      if (names.has(m[1])) found.add(m[1]);
    }
    // Only formats with a fixed name start ("wifi-strength-"), never a bare "%s".
    for (const m of text.matchAll(/"([a-z][a-z0-9]*(?:-[a-z0-9]+)*-%0?\d*[uds](?:[a-z0-9-]|%0?\d*[uds])*)"/g)) {
      patterns.add(m[1]);
    }
  }
  for (const format of patterns) {
    const pattern = new RegExp(`^${format.replace(/%0?\d*[ud]/g, '\\d+').replace(/%s/g, '[a-z0-9-]*')}$`);
    for (const name of names.keys()) if (pattern.test(name)) found.add(name);
  }
  return found;
}

// The Bridge's own default icons ("mdi:..." literals of the integration).
export function bridgeIconNames(bridgeRoot, names = iconNameTable()) {
  const directory = path.join(bridgeRoot, 'custom_components', 'tab5_lvgl');
  const found = new Set();
  for (const file of sourceFiles(directory, ['.py'], [])) {
    for (const m of fs.readFileSync(file, 'utf8').matchAll(/["']mdi:([a-z0-9]+(?:-[a-z0-9]+)*)["']/g)) {
      if (names.has(m[1])) found.add(m[1]);
    }
  }
  return found;
}

// Entity domains whose Home Assistant default icons stay built in, so a
// dashboard without the icon file still shows HA's usual icons.
export const HA_DOMAINS = [
  'air_quality', 'alarm_control_panel', 'automation', 'binary_sensor', 'button', 'calendar', 'camera',
  'climate', 'counter', 'cover', 'date', 'datetime', 'device_tracker', 'event', 'fan', 'humidifier', 'image',
  'input_boolean', 'input_button', 'input_datetime', 'input_number', 'input_select', 'input_text',
  'lawn_mower', 'light', 'lock', 'media_player', 'notify', 'number', 'person', 'remote', 'scene', 'script',
  'select', 'sensor', 'siren', 'sun', 'switch', 'text', 'time', 'timer', 'todo', 'update', 'vacuum', 'valve',
  'water_heater', 'weather', 'zone',
];

// The icons of the entity_component sections of Home Assistant's icons.json
// files (default, state and range icons of every device class) of one release.
export async function homeAssistantIconNames(tag, names = iconNameTable()) {
  const found = new Set();
  const collect = (value) => {
    if (typeof value === 'string') {
      if (value.startsWith('mdi:') && names.has(value.slice(4))) found.add(value.slice(4));
    } else if (value && typeof value === 'object') {
      Object.values(value).forEach(collect);
    }
  };
  for (const domain of HA_DOMAINS) {
    const url = `https://raw.githubusercontent.com/home-assistant/core/${tag}/homeassistant/components/${domain}/icons.json`;
    const response = await fetch(url);
    if (response.status === 404) continue;
    if (!response.ok) throw new Error(`${url}: HTTP ${response.status}`);
    collect((await response.json()).entity_component || {});
  }
  return found;
}

// tools/icons/core-icons.txt: one name per line, "#" starts a comment.
export function readCoreList() {
  const text = fs.readFileSync(path.join(root, CORE_LIST), 'utf8');
  return text.split(/\r?\n/).map((line) => line.replace(/#.*/, '').trim()).filter(Boolean);
}

function writeCoreList(firmware, bridge, homeAssistant, haTag) {
  const bridgeOnly = [...bridge].filter((name) => !firmware.has(name)).sort();
  const haOnly = [...homeAssistant].filter((name) => !firmware.has(name) && !bridge.has(name)).sort();
  const lines = [
    '# MDI icons built into the firmware, shown even without the icon file.',
    '# Generated by tools/generate-icon-files.mjs --update-core; the icon file',
    '# test checks that every icon name of the firmware sources is listed.',
    '',
    '# Named in the firmware sources',
    ...[...firmware].sort(),
    '',
    '# Default icons of the HomeTiles Bridge',
    ...bridgeOnly,
    '',
    `# Home Assistant ${haTag} default icons of the entity domains`,
    ...haOnly,
    '',
  ];
  fs.mkdirSync(path.dirname(path.join(root, CORE_LIST)), {recursive: true});
  fs.writeFileSync(path.join(root, CORE_LIST), lines.join('\n'));
  return firmware.size + bridgeOnly.length + haOnly.length;
}

function argument(name) {
  const index = process.argv.indexOf(name);
  return index >= 0 ? process.argv[index + 1] : undefined;
}

async function main() {
  if (process.argv.includes('--update-core')) {
    const names = iconNameTable();
    const bridgeRoot = argument('--bridge') || path.join(root, '..', 'HomeTiles_Bridge');
    if (!fs.existsSync(path.join(bridgeRoot, 'custom_components', 'tab5_lvgl'))) {
      throw new Error(`HomeTiles Bridge not found at ${bridgeRoot}; pass --bridge <path>`);
    }
    const haTag = argument('--ha');
    if (!haTag) throw new Error('pass --ha <Home Assistant release tag>, for example --ha 2026.10.1');
    const count = writeCoreList(firmwareIconNames(names), bridgeIconNames(bridgeRoot, names),
      await homeAssistantIconNames(haTag, names), haTag);
    console.log(`Wrote ${CORE_LIST}: ${count} icons`);
    return;
  }
  const out = path.resolve(argument('--out') || path.join(root, 'build', 'icons'));
  fs.mkdirSync(out, {recursive: true});
  const filesOnly = process.argv.includes('--files-only');
  const core = readCoreList();
  const names = iconNameTable();
  for (const px of SIZES) {
    const source = fs.readFileSync(fontSourcePath(px), 'utf8');
    const font = parseMdiFont(source, `mdi_icons_${px}`);
    const file = buildIconFile(font, px);
    const target = path.join(out, iconFileName(px));
    fs.writeFileSync(target, file);
    const {header} = readIconFile(file);
    const coreIds = coreGlyphIds(font, core, names);
    if (!filesOnly) fs.writeFileSync(fontOutputPath(px), compiledFontSource(source, font, px, coreIds));
    console.log(`${target}: ${file.length} bytes, ${header.glyphs} glyphs, ` +
      `table CRC ${header.tableCrc.toString(16).padStart(8, '0')}` +
      (filesOnly ? '' : `; ${path.relative(root, fontOutputPath(px))}: ${coreIds.length} core icons`));
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) await main();
