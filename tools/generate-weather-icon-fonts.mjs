#!/usr/bin/env node
// Generates the filled, multi-color weather icons:
//   src/fonts/weather_icons_<32|40|48>.c  one glyph per icon layer
//   src/types/weather/weather_icon_table.h  label text per MDI icon name
//   src/types/weather/admin-icons.js  the same layers as SVG for the Web Admin
// from tools/weather-icons/parts.mjs. The fonts use the same converter as the
// MDI icon fonts (lv_font_conv 1.5.3, downloaded with npm like
// generate-mdi-fonts.ps1) and the opentype.js bundled with it. Each font falls
// back to the matching MDI font and copies its line metrics, so a weather icon
// label shows every other MDI icon exactly as before.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createRequire} from 'node:module';
import {execFileSync, execSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {ICONS, TINTS} from './weather-icons/parts.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const CONVERTER = 'lv_font_conv@1.5.3';
// MDI glyphs map the 24-unit grid onto the em square with the baseline at
// grid y 21 (ascent 21, descent 3).
const UNIT = 100;
const EM = 24 * UNIT;
const ASCENT = 21 * UNIT;
const FIRST_CODEPOINT = 0xE000;  // spacer; layer glyphs follow
const TARGETS = [
  {size: 32, layout: 'defined(DEVICE_LAYOUT_480X480)'},
  {size: 40, layout: 'defined(DEVICE_LAYOUT_1024X600)'},
  {size: 48, layout: '!defined(DEVICE_LAYOUT_1024X600) && !defined(DEVICE_LAYOUT_480X480)'},
];

// --- SVG path data to absolute segments --------------------------------------

function arcToCubics(x1, y1, rx, ry, rotation, large, sweep, x2, y2) {
  if (!rx || !ry) return [{t: 'L', x: x2, y: y2}];
  const phi = rotation * Math.PI / 180, cos = Math.cos(phi), sin = Math.sin(phi);
  const hx = (x1 - x2) / 2, hy = (y1 - y2) / 2;
  const x1p = cos * hx + sin * hy, y1p = -sin * hx + cos * hy;
  rx = Math.abs(rx); ry = Math.abs(ry);
  const lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
  if (lambda > 1) { rx *= Math.sqrt(lambda); ry *= Math.sqrt(lambda); }
  const num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
  const den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
  let coef = Math.sqrt(Math.max(0, num / den));
  if (large === sweep) coef = -coef;
  const cxp = coef * rx * y1p / ry, cyp = -coef * ry * x1p / rx;
  const cx = cos * cxp - sin * cyp + (x1 + x2) / 2;
  const cy = sin * cxp + cos * cyp + (y1 + y2) / 2;
  const angle = (ux, uy, vx, vy) => Math.atan2(ux * vy - uy * vx, ux * vx + uy * vy);
  const start = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
  let delta = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
  if (!sweep && delta > 0) delta -= 2 * Math.PI;
  else if (sweep && delta < 0) delta += 2 * Math.PI;
  const count = Math.max(1, Math.ceil(Math.abs(delta) / (Math.PI / 2) - 1e-9));
  const step = delta / count, k = 4 / 3 * Math.tan(step / 4);
  const point = (ex, ey) => ({x: cos * rx * ex - sin * ry * ey + cx, y: sin * rx * ex + cos * ry * ey + cy});
  const out = [];
  for (let i = 0, a = start; i < count; i++, a += step) {
    const c1 = Math.cos(a), s1 = Math.sin(a), c2 = Math.cos(a + step), s2 = Math.sin(a + step);
    const p1 = point(c1 - k * s1, s1 + k * c1), p2 = point(c2 + k * s2, s2 - k * c2), end = point(c2, s2);
    out.push({t: 'C', x1: p1.x, y1: p1.y, x2: p2.x, y2: p2.y, x: end.x, y: end.y});
  }
  Object.assign(out.at(-1), {x: x2, y: y2});
  return out;
}

// Parses the absolute commands the MDI weather paths use (M L H V C A Z).
export function parsePath(d) {
  const tokens = d.match(/[A-Za-z]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?/g) || [];
  const out = [];
  let i = 0, cmd = '', x = 0, y = 0, sx = 0, sy = 0;
  const num = () => {
    const value = Number(tokens[i++]);
    if (!Number.isFinite(value)) throw new Error(`Bad number in path: ${d}`);
    return value;
  };
  while (i < tokens.length) {
    if (/[A-Za-z]/.test(tokens[i])) {
      cmd = tokens[i++];
      if (!'MLHVCAZ'.includes(cmd)) throw new Error(`Unsupported path command ${cmd}`);
      if (cmd === 'Z') { out.push({t: 'Z'}); x = sx; y = sy; continue; }
    } else if (cmd === 'Z' || !cmd) {
      throw new Error(`Number without command in path: ${d}`);
    }
    switch (cmd) {
      case 'M': x = sx = num(); y = sy = num(); out.push({t: 'M', x, y}); cmd = 'L'; break;
      case 'L': x = num(); y = num(); out.push({t: 'L', x, y}); break;
      case 'H': x = num(); out.push({t: 'L', x, y}); break;
      case 'V': y = num(); out.push({t: 'L', x, y}); break;
      case 'C': {
        const x1 = num(), y1 = num(), x2 = num(), y2 = num();
        x = num(); y = num();
        out.push({t: 'C', x1, y1, x2, y2, x, y});
        break;
      }
      case 'A': {
        const rx = num(), ry = num(), rot = num(), large = num(), sweep = num(), ex = num(), ey = num();
        out.push(...arcToCubics(x, y, rx, ry, rot, large, sweep, ex, ey));
        x = ex; y = ey;
        break;
      }
    }
  }
  return out;
}

// --- Glyphs and label texts ---------------------------------------------------

const layerKey = (layer) => JSON.stringify(layer.paths);
const layers = [];
const layerIndex = new Map();
for (const icon of Object.values(ICONS)) {
  for (const layer of icon) {
    const key = layerKey(layer);
    if (!layerIndex.has(key)) { layerIndex.set(key, layers.length); layers.push(layer); }
  }
}
const layerCodepoint = (layer) => FIRST_CODEPOINT + 1 + layerIndex.get(layerKey(layer));
const lastCodepoint = FIRST_CODEPOINT + layers.length;

const utf8Escape = (codepoint) =>
  [...Buffer.from(String.fromCodePoint(codepoint), 'utf8')].map((b) => '\\x' + b.toString(16).toUpperCase()).join('');

function labelText(icon) {
  // "#RRGGBB <glyph>#" per layer; the literals are split so a hex escape never
  // runs into the next character.
  const parts = icon.map((layer) => `"${layer.color.toUpperCase()} " "${utf8Escape(layerCodepoint(layer))}" "#"`);
  return `${parts.join(' ')} "${utf8Escape(FIRST_CODEPOINT)}"`;
}

// The same layers without recolor commands, drawn in the label color.
function plainText(icon) {
  return icon.map((layer) => `"${utf8Escape(layerCodepoint(layer))}"`).join(' ') + ` "${utf8Escape(FIRST_CODEPOINT)}"`;
}

function tint(name) {
  const color = TINTS[name];
  if (!/^#[0-9A-F]{6}$/i.test(color || '')) throw new Error(`Missing weather color for ${name}`);
  return '0x' + color.slice(1).toUpperCase();
}

// Codepoints of the generated fonts, for tests.
export const WEATHER_ICON_CODEPOINTS = {first: FIRST_CODEPOINT, last: lastCodepoint, layers: layers.length};

export function weatherIconTableSource() {
  const entries = Object.entries(ICONS)
    .map(([name, icon]) =>
      `    {"${name}",\n     ${labelText(icon)},\n     ${plainText(icon)},\n     ${tint(name)}},`)
    .join('\n');
  return `// Generated by tools/generate-weather-icon-fonts.mjs from
// tools/weather-icons/parts.mjs. Do not edit by hand.
#pragma once

#include <stdint.h>

namespace weather_icon_table {

// Per MDI weather icon name, for the weather icon font with label recoloring:
// - text: every "#RRGGBB <glyph>#" draws one icon layer in its color. Layer
//   glyphs have no advance, so all layers share one position; the closing
//   spacer glyph gives the icon the MDI advance width.
// - plain: the same layers without colors, drawn in the label color (an icon
//   color the user chose).
// - tint: the weather color, the label color of a colored icon, so Tile color
//   "From icon" and the icon disc follow the weather.
struct Entry {
  const char* name;
  const char* text;
  const char* plain;
  uint32_t tint;
};

inline constexpr uint32_t kFirstCodepoint = 0x${FIRST_CODEPOINT.toString(16).toUpperCase()};
inline constexpr uint32_t kLastCodepoint = 0x${lastCodepoint.toString(16).toUpperCase()};

inline constexpr Entry kEntries[] = {
${entries}
};

}  // namespace weather_icon_table
`;
}

function writeTable() {
  const file = path.join(root, 'src/types/weather/weather_icon_table.h');
  fs.writeFileSync(file, weatherIconTableSource());
  console.log(`Generated ${path.relative(root, file)} (${Object.keys(ICONS).length} icons, ${layers.length} layers)`);
}

// --- Web Admin ------------------------------------------------------------------

// One SVG path per layer path, in the 24-unit MDI grid of the glyphs.
function layerSvg(layer) {
  return layer.paths.map(({d, t}) => {
    const transform = t ? ` transform="translate(${+t.dx.toFixed(4)} ${+t.dy.toFixed(4)}) scale(${t.s})"` : '';
    return `<path d="${d}"${transform}/>`;
  }).join('');
}

export function weatherIconWebSource() {
  const paths = layers.map((layer) => `    '${layerSvg(layer)}',`).join('\n');
  const icons = Object.entries(ICONS).map(([name, icon]) => {
    tint(name);
    const parts = icon.map((layer) => `['${layer.color.toUpperCase()}', ${layerIndex.get(layerKey(layer))}]`);
    return `    '${name}': {tint: '${TINTS[name].toUpperCase()}', layers: [${parts.join(', ')}]},`;
  }).join('\n');
  return `// Generated by tools/generate-weather-icon-fonts.mjs from
// tools/weather-icons/parts.mjs. Do not edit by hand.
// The device's filled, multi-color weather icons (weather_icon_table.h) as SVG
// for the Web Admin preview: shared layer paths in the 24-unit MDI grid, and
// per icon its weather color and its layers bottom to top.
  const WEATHER_ICON_LAYER_PATHS = Object.freeze([
${paths}
  ]);
  const WEATHER_ICONS = Object.freeze({
${icons}
  });
`;
}

function writeWebIcons() {
  const file = path.join(root, 'src/types/weather/admin-icons.js');
  fs.writeFileSync(file, weatherIconWebSource());
  console.log(`Generated ${path.relative(root, file)}`);
}

// --- Fonts ----------------------------------------------------------------------

function glyphPath(opentype, layer) {
  const out = new opentype.Path();
  const X = (v) => Math.round(v * UNIT);
  const Y = (v) => Math.round(ASCENT - v * UNIT);
  for (const {d, t = {s: 1, dx: 0, dy: 0}} of layer.paths) {
    const map = (px, py) => [X(t.s * px + t.dx), Y(t.s * py + t.dy)];
    let open = false;
    for (const seg of parsePath(d)) {
      if (seg.t === 'M') { if (open) out.close(); out.moveTo(...map(seg.x, seg.y)); open = true; }
      else if (seg.t === 'L') out.lineTo(...map(seg.x, seg.y));
      else if (seg.t === 'C') out.curveTo(...map(seg.x1, seg.y1), ...map(seg.x2, seg.y2), ...map(seg.x, seg.y));
      else if (seg.t === 'Z' && open) { out.close(); open = false; }
    }
    if (open) out.close();
  }
  return out;
}

function mdiMetrics(size) {
  const source = fs.readFileSync(path.join(root, `src/fonts/mdi_icons_${size}.c`), 'utf8');
  const lineHeight = Number(source.match(/\.line_height = (\d+),/)[1]);
  const baseLine = Number(source.match(/\.base_line = (-?\d+),/)[1]);
  return {lineHeight, baseLine};
}

function postProcess(source, {size, layout}) {
  const name = `weather_icons_${size}`;
  const macro = name.toUpperCase();
  const {lineHeight, baseLine} = mdiMetrics(size);
  const replace = (pattern, replacement) => {
    if (!pattern.test(source)) throw new Error(`${name}: pattern not found: ${pattern}`);
    source = source.replace(pattern, replacement);
  };
  replace(/^ \* Opts: .+$/m,
    ` * Opts: --bpp 4 --size ${size} --font weather-icons.otf ` +
    `--range 0x${FIRST_CODEPOINT.toString(16).toUpperCase()}-0x${lastCodepoint.toString(16).toUpperCase()} ` +
    `--format lvgl --lv-font-name ${name}\n` +
    ` * Generated by tools/generate-weather-icon-fonts.mjs. Do not edit by hand.`);
  replace(/^#ifdef LV_LVGL_H_INCLUDE_SIMPLE\r?\n#include "lvgl\.h"\r?\n#else\r?\n#include "lvgl\/lvgl\.h"\r?\n#endif/m,
    '#include "lvgl.h"');
  replace(new RegExp(`^#ifndef ${macro}\\r?\\n#define ${macro} 1\\r?\\n#endif`, 'm'),
    `#include "src/devices/device_select.h"\n\n#ifndef ${macro}\n#if ${layout}\n#define ${macro} 1\n` +
    `#else\n#define ${macro} 0\n#endif\n#endif`);
  replace(new RegExp(`^#if ${macro}$`, 'm'),
    `#if ${macro}\n\n// Every icon that is not a weather layer comes from the MDI font.\nLV_FONT_DECLARE(mdi_icons_${size})`);
  // Same line box as the MDI font, so a label keeps its size and baseline.
  replace(/\.line_height = \d+,/, `.line_height = ${lineHeight},`);
  replace(/\.base_line = -?\d+,/, `.base_line = ${baseLine},`);
  replace(/\.fallback = NULL,/, `.fallback = &mdi_icons_${size},`);
  return source.replace(/\r\n/g, '\n').trimEnd() + '\n';
}

function writeFonts() {
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'HomeTiles-weather-icons-'));
  try {
    execSync(`npm pack ${CONVERTER} --pack-destination "${temp}"`, {stdio: 'ignore'});
    const archive = fs.readdirSync(temp).find((f) => f.endsWith('.tgz'));
    if (!archive) throw new Error(`Unable to download ${CONVERTER}`);
    execFileSync('tar', ['-xf', archive, '-C', '.'], {cwd: temp});
    const pkg = path.join(temp, 'package');
    const opentype = createRequire(path.join(pkg, 'package.json'))('opentype.js');

    const glyphs = [
      new opentype.Glyph({name: '.notdef', advanceWidth: EM, path: new opentype.Path()}),
      new opentype.Glyph({name: 'spacer', unicode: FIRST_CODEPOINT, advanceWidth: EM, path: new opentype.Path()}),
      ...layers.map((layer, i) => new opentype.Glyph({
        name: `layer${i + 1}`, unicode: FIRST_CODEPOINT + 1 + i, advanceWidth: 0, path: glyphPath(opentype, layer),
      })),
    ];
    const font = new opentype.Font({
      familyName: 'HomeTilesWeatherIcons', styleName: 'Regular',
      unitsPerEm: EM, ascender: ASCENT, descender: ASCENT - EM, glyphs,
    });
    const otf = path.join(temp, 'weather-icons.otf');
    fs.writeFileSync(otf, Buffer.from(font.toArrayBuffer()));

    for (const target of TARGETS) {
      const name = `weather_icons_${target.size}`;
      const generated = path.join(temp, `${name}.c`);
      execFileSync(process.execPath, [
        path.join(pkg, 'lv_font_conv.js'), '--bpp', '4', '--size', String(target.size),
        '--font', otf, '--range', `0x${FIRST_CODEPOINT.toString(16)}-0x${lastCodepoint.toString(16)}`,
        '--format', 'lvgl', '--lv-font-name', name, '-o', generated,
      ], {stdio: 'inherit'});
      const output = path.join(root, 'src/fonts', `${name}.c`);
      fs.writeFileSync(output, postProcess(fs.readFileSync(generated, 'utf8'), target));
      console.log(`Generated ${path.relative(root, output)}`);
    }
  } finally {
    fs.rmSync(temp, {recursive: true, force: true});
  }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  writeTable();
  writeWebIcons();
  if (!process.argv.includes('--tables-only')) writeFonts();
}
