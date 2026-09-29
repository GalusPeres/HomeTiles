// Circles, controls and icons from the icon color (src/ui/shared/tone_color.h),
// agreed in the Farblabor: every color gets the same visible circle (a fixed
// OKLCH lightness step above the card) instead of a fixed opacity that made
// Indigo nearly invisible and Yellow loud; the controls show exactly the
// circle's color; an icon keeps its color while readable and a darker one is
// raised continuously in its own hue (a circle flipping above the icon was
// rejected as abrupt). The Web Admin preview computes the same colors.
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
  assert.equal(fill.controlOpa, fill.discOpa, 'at 25 % the controls show the circle color');
  const shown = js.toneReadableIcon(rgb(icon), fill.disc);
  const seed = js.toneOklch(rgb(icon));
  if (seed.L >= js.toneOklch(fill.disc).L + 0.22) assert.deepEqual(shown, rgb(icon), 'a readable icon keeps its color');
  else assert.ok(js.toneOklch(shown).L >= js.toneOklch(fill.disc).L + 0.215, 'a dark icon reaches the minimum step');
}
// Continuous: raising an icon's lightness never makes the shown icon jump.
{
  const c = card([0x28, 0x33, 0x71]);
  const fill = js.toneFill(c, [0x28, 0x33, 0x71], true, 25);
  let previous = null;
  for (let v = 0; v <= 255; v += 3) {
    const shownL = js.toneOklch(js.toneReadableIcon([Math.round(v * 0.35), Math.round(v * 0.45), v], fill.disc)).L;
    if (previous !== null) assert.ok(shownL >= previous - 0.004 && shownL - previous < 0.03, 'the icon lightness moves smoothly');
    previous = shownL;
  }
}
// Below 12.5 % the circle fades while the controls keep their minimum.
{
  const fill = js.toneFill([34, 34, 34], [255, 255, 255], false, 0);
  assert.equal(fill.discOpa, 0);
  assert.equal(fill.controlOpa, 32);
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
for (const icon of icons) for (const percent of [0, 5, 25, 60, 100]) cases.push([hex(card(rgb(icon))), icon, icon !== 0xFFFFFF, percent]);
const cpp = `#include <cstdio>
#include "src/ui/shared/tone_color.h"
int main() {
  const struct { unsigned card, icon; bool tinted; unsigned percent; } cases[] = {
${cases.map(([c, i, t, p]) => `    {${c}u, ${i}u, ${t}, ${p}u},`).join('\n')}
  };
  for (const auto& c : cases) {
    const tone_color::Fill f = tone_color::fill(c.card, c.icon, c.tinted, static_cast<uint8_t>(c.percent));
    std::printf("%u %u %u %u %u\\n", f.color, f.disc_opa, f.control_opa, f.disc, tone_color::readable_icon(c.icon, f.disc));
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
  const [color, discOpa, controlOpa, disc, shown] = line.split(' ').map(Number);
  const [c, i, t, p] = cases[index];
  const fill = js.toneFill(rgb(c), rgb(i), t, p);
  const what = `#${c.toString(16)} / #${i.toString(16)} @ ${p} %`;
  assert.equal(discOpa, fill.discOpa, what);
  assert.equal(controlOpa, fill.controlOpa, what);
  assert.ok(near(color, hex(fill.color)) && near(disc, hex(fill.disc)), `${what}: device #${color.toString(16)} preview #${hex(fill.color).toString(16)}`);
  assert.ok(near(shown, hex(js.toneReadableIcon(rgb(i), fill.disc))), `${what}: readable icon`);
});
console.log('Tone colors: same circle step for every hue, controls = circle, smooth icon lift, device == preview');
