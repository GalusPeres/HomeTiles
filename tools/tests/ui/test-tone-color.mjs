// Circles, controls and icons from the icon color (src/ui/shared/tone_color.h),
// agreed in the Farblabor: every color gets the same visible circle (a fixed
// OKLCH lightness step above the card) instead of a fixed opacity that made
// Indigo nearly invisible and Yellow loud; the controls show exactly the
// circle's color; an icon keeps its color while readable and a darker one is
// raised continuously in its own hue (a circle flipping above the icon was
// rejected as abrupt). Circles and controls are drawn opaque in exactly these
// colors: a translucent fill lost half the step on the panels' 16-bit
// blending and could not reach dark saturated circles over grey; only
// see-through screensaver tiles keep the veil. The Web Admin preview computes
// the same colors.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extractDeliveredFunction} from '../../lib/admin-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const tone = read('src/ui/shared/tone_color.h');
for (const marker of [
  'inline constexpr float kStepPerPercent = 0.0024f;',
  'inline constexpr float kControlMinStep = 0.03f;',
  'inline constexpr uint8_t kControlMinOpa = 32;',
  'inline constexpr float kIconMinStep = 0.22f;',
  'inline constexpr float kCircleChroma = 0.55f;',
]) assert.ok(tone.includes(marker), `tone_color.h: ${marker}`);

// The preview functions, exactly as delivered to the browser.
const js = new Function(['toneToLinear', 'toneToSrgb', 'toneOklch', 'toneLinear', 'toneRgb', 'toneBlend', 'toneFill',
  'toneReadableIcon'].map(extractDeliveredFunction).join('\n') + '; return {toneFill, toneReadableIcon, toneOklch};')();

// The tile card of Tile color "From icon" at 20 % (tile_tint::background).
const lin = v => { v /= 255; return v <= 0.04045 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); };
const luminance = c => 0.2126 * lin(c[0]) + 0.7152 * lin(c[1]) + 0.0722 * lin(c[2]);
const card = icon => {
  let out = [26, 26, 26].map((v, i) => Math.floor((v * 80 + icon[i] * 20 + 50) / 100));
  for (let i = 0; i < 40 && 1.05 / (luminance(out) + 0.05) < 4.5; i++) out = out.map(v => Math.floor((v * 95 + 50) / 100));
  return out;
};
const rgb = hex => [16, 8, 0].map(s => (hex >> s) & 255);
const hex = c => (c[0] << 16) | (c[1] << 8) | c[2];
const icons = [0xF44336, 0xE91E63, 0x9C27B0, 0x673AB7, 0x3F51B5, 0x2196F3, 0x03A9F4, 0x00BCD4, 0x009688, 0x4CAF50,
  0x8BC34A, 0xCDDC39, 0xFFEB3B, 0xFFC107, 0xFF9800, 0xFF5722, 0x795548, 0x283371, 0x1D223F, 0xFFFFFF];

// Same visible step for every hue at the default 25 %; the controls are the
// circle; bright icons stay untouched, dark ones reach the minimum step.
for (const icon of icons) {
  const c = card(rgb(icon));
  const tinted = !(icon === 0xFFFFFF);
  const fill = js.toneFill(c, rgb(icon), tinted, 25);
  const lift = js.toneOklch(fill.disc).L - js.toneOklch(c).L;
  assert.ok(Math.abs(lift - 0.06) <= 0.006, `#${icon.toString(16)}: circle step ${lift.toFixed(3)}`);
  assert.equal(fill.discOpa, 255, 'the circle is opaque');
  assert.equal(fill.controlOpa, 255, 'the controls are opaque');
  assert.deepEqual(fill.discColor, fill.disc, 'the circle draws exactly its color');
  assert.deepEqual(fill.controlColor, fill.disc, 'at 25 % the controls show the circle color');
  // With the default settings the icon is measured against exactly this
  // circle.
  const shown = js.toneReadableIcon(rgb(icon));
  const seed = js.toneOklch(rgb(icon));
  if (seed.L >= js.toneOklch(fill.disc).L + 0.22) assert.deepEqual(shown, rgb(icon), 'a readable icon keeps its color');
  else assert.ok(js.toneOklch(shown).L >= js.toneOklch(fill.disc).L + 0.215, 'a dark icon reaches the minimum step');
}
// Continuous: raising an icon's lightness never makes the shown icon jump.
{
  let previous = null;
  for (let v = 12; v <= 255; v += 3) {
    const shownL = js.toneOklch(js.toneReadableIcon([Math.round(v * 0.35), Math.round(v * 0.45), v])).L;
    if (previous !== null) assert.ok(shownL >= previous - 0.004 && shownL - previous < 0.03, 'the icon lightness moves smoothly');
    previous = shownL;
  }
}
// Regression (user 2026-10-01): a lighter global tile color and a stronger
// circle lifted red, green, blue and orange icons towards pastel. An icon's
// shown color depends only on the icon color: device and preview measure it
// against the circle of the default settings, never the circle on screen.
assert.ok(tone.includes('inline uint32_t readable_icon(uint32_t icon) {') &&
  tone.includes('tinted ? tile_tint::background(tile_color::kDefault, icon, kReferenceTint) : tile_color::kDefault;') &&
  tone.includes('lifted(card, icon, tinted, icon_glow::kDefault * kStepPerPercent);'));
assert.match(read('src/tiles/config/tile_icon_colors.h'), /inline constexpr uint8_t kTintDefault = 20;/);
assert.ok(tone.includes('inline constexpr uint8_t kReferenceTint = 20;'), 'reference = From icon default strength');
assert.ok(read('src/web/admin/tiles/grid-preview.js').includes('const readable = toneReadableIcon(given);'));
for (const icon of [0xF44336, 0x4CAF50, 0x2196F3, 0xFF9800, 0xFFD54F, 0x00BCD4, 0xFFFFFF, 0xB0B0B0]) {
  assert.deepEqual(js.toneReadableIcon(rgb(icon)), rgb(icon), `#${icon.toString(16)} keeps its color`);
}
assert.deepEqual(js.toneReadableIcon(rgb(0x9C27B0)), [0xA8, 0x35, 0xBC], 'a dark purple is lifted like before');

// Without the icon color the circle is the tile's own color a step lighter,
// never a grey patch on a colored tile (regression b109: MISC, Sonos, PC).
for (const tile of [[0xA3, 0x3B, 0x3B], [0x6B, 0x5F, 0x2B], [0x3D, 0x22, 0x55], [0x7A, 0x24, 0x10]]) {
  const fill = js.toneFill(tile, [255, 255, 255], false, 25);
  const a = js.toneOklch(tile), b = js.toneOklch(fill.disc);
  const hue = Math.abs(Math.atan2(Math.sin(a.h - b.h), Math.cos(a.h - b.h)));
  assert.ok(hue < 0.12 && b.C >= a.C * 0.8, `neutral circle keeps the tile hue: ${tile} -> ${fill.disc}`);
  assert.ok(Math.abs(b.L - a.L - 0.06) <= 0.006, 'neutral circle: the same step');
}
// Tiles the script does not tint (the parked Settings tile) take the neutral
// circle of the global tile color from the page, transparent at 0 %.
assert.ok(read('src/web/server/render/web_admin_styles.cpp').includes(
  'const tone_color::Fill fill = tone_color::fill(tileDefaultBgColor(), 0xFFFFFF, false, glow);'));
// Over the global grey a colored circle keeps the icon's hue and exactly its
// step (regression: the translucent fill kept a grey floor, an orange circle
// #4D1F00 showed as red-brown #4D1F11).
for (const icon of icons.filter(i => i !== 0xFFFFFF)) {
  const grey = [26, 26, 26];
  const fill = js.toneFill(grey, rgb(icon), true, 35);
  const want = js.toneOklch(rgb(icon)).h, got = js.toneOklch(fill.disc);
  if (got.C > 0.04) {
    assert.ok(Math.abs(Math.atan2(Math.sin(want - got.h), Math.cos(want - got.h))) < 0.06,
      `#${icon.toString(16)} over grey: circle hue`);
  }
  assert.ok(Math.abs(got.L - js.toneOklch(grey).L - 0.084) <= 0.006, `#${icon.toString(16)} over grey: step`);
}
assert.deepEqual(js.toneFill([26, 26, 26], [0xFF, 0x8A, 0x3D], true, 35).disc, [0x4D, 0x1F, 0x00]);
// The neutral circle over the global grey is exactly its step (#282828).
assert.deepEqual(js.toneFill([26, 26, 26], [255, 255, 255], false, 25).disc, [40, 40, 40]);
// Below 12.5 % the circle keeps its smaller step while the controls keep
// their minimum; at 0 % there is no circle.
{
  const fill = js.toneFill([34, 34, 34], [255, 255, 255], false, 0);
  assert.equal(fill.discOpa, 0);
  assert.equal(fill.controlOpa, 255);
  assert.ok(Math.abs(js.toneOklch(fill.control).L - js.toneOklch([34, 34, 34]).L - 0.03) <= 0.004);
  const low = js.toneFill([34, 34, 34], [255, 255, 255], false, 5);
  assert.ok(Math.abs(js.toneOklch(low.disc).L - js.toneOklch([34, 34, 34]).L - 0.012) <= 0.004, 'circle at 5 %');
  assert.deepEqual(low.control, fill.control, 'controls keep their minimum step');
}
// See-through cards (screensaver tiles below full Tile opacity) keep the
// veil: the circle at the strength's opacity, the controls at least 32.
{
  const veil = js.toneFill([34, 34, 34], [255, 255, 255], false, 25, true);
  assert.equal(veil.discOpa, 64);
  assert.equal(veil.controlOpa, 64);
  assert.deepEqual(veil.discColor, veil.controlColor);
  assert.ok(Math.abs(js.toneOklch(veil.disc).L - js.toneOklch([34, 34, 34]).L - 0.06) <= 0.006, 'veil step');
  const faint = js.toneFill([34, 34, 34], [255, 255, 255], false, 0, true);
  assert.equal(faint.discOpa, 0);
  assert.equal(faint.controlOpa, 32);
}

const host = await lvglHost(root);
if (!host) {
  console.log('Tone colors: rule and preview pass; SKIP: device comparison needs a host compiler');
  process.exit(0);
}
// The device math gives the same colors as the preview (float vs. double:
// at most one step per channel).
const out = path.join(root, 'build/tests/tone-color');
fs.mkdirSync(out, {recursive: true});
const cases = [];
for (const icon of icons) {
  for (const percent of [0, 5, 25, 60, 100]) {
    for (const seeThrough of [false, true]) cases.push([hex(card(rgb(icon))), icon, icon !== 0xFFFFFF, percent, seeThrough]);
  }
  cases.push([0x1A1A1A, icon, icon !== 0xFFFFFF, 35, false]);
}
const cpp = `#include <cstdio>
#include "src/ui/shared/tone_color.h"
int main() {
  const struct { unsigned card, icon; bool tinted; unsigned percent; bool see_through; } cases[] = {
${cases.map(([c, i, t, p, s]) => `    {${c}u, ${i}u, ${t}, ${p}u, ${s}},`).join('\n')}
  };
  for (const auto& c : cases) {
    const tone_color::Fill f =
        tone_color::fill(c.card, c.icon, c.tinted, static_cast<uint8_t>(c.percent), c.see_through);
    std::printf("%u %u %u %u %u %u %u\\n", f.disc_color, f.control_color, f.disc_opa, f.control_opa, f.disc,
                f.control, tone_color::readable_icon(c.icon));
  }
  return 0;
}
`;
const source = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(source, cpp);
let result = spawnSync(host.cxx, ['-std=c++17', '-I', root, source, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stderr);
const near = (a, b) => rgb(a).every((v, i) => Math.abs(v - rgb(b)[i]) <= 1);
result.stdout.trim().split(/\r?\n/).forEach((line, index) => {
  const [discColor, controlColor, discOpa, controlOpa, disc, control, shown] = line.split(' ').map(Number);
  const [c, i, t, p, s] = cases[index];
  const fill = js.toneFill(rgb(c), rgb(i), t, p, s);
  const what = `#${c.toString(16)} / #${i.toString(16)} @ ${p} %${s ? ' see-through' : ''}`;
  assert.equal(discOpa, fill.discOpa, what);
  assert.equal(controlOpa, fill.controlOpa, what);
  assert.ok(near(discColor, hex(fill.discColor)) && near(controlColor, hex(fill.controlColor)) &&
    near(disc, hex(fill.disc)) && near(control, hex(fill.control)),
    `${what}: device #${discColor.toString(16)} preview #${hex(fill.discColor).toString(16)}`);
  assert.ok(near(shown, hex(js.toneReadableIcon(rgb(i)))), `${what}: readable icon`);
});
console.log('Tone colors: same opaque circle step for every hue, controls = circle, veil on see-through tiles, smooth icon lift, device == preview');
