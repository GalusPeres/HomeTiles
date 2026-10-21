// Issue #115: Home Assistant writes "μg/m³" with the Greek mu U+03BC, which
// the panel fonts did not carry, so the unit lost its first letter. Every UI
// font that carries the micro sign U+00B5 must find the same glyph for the
// Greek mu, by LVGL's own cmap lookup, and declare every cmap it lists.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

import {ALIASES, glyphId, readCmaps, uiFontFiles, withAliases} from '../../font-aliases.mjs';

assert.deepEqual(ALIASES.find(([alias]) => alias === 0x03BC), [0x03BC, 0x00B5], 'Greek mu draws as the micro sign');

let checked = 0;
for (const file of uiFontFiles()) {
  const name = path.basename(file);
  const source = fs.readFileSync(file, 'utf8');
  const cmaps = readCmaps(source);
  assert.ok(cmaps, `${name}: cmap table found`);
  assert.equal(Number(/\.cmap_num = (\d+),/.exec(source)[1]), cmaps.length, `${name}: cmap_num counts every entry`);
  for (const [alias, carried] of ALIASES) {
    const target = glyphId(cmaps, carried);
    if (!target) continue;  // digit-only value fonts (80, 96)
    assert.equal(glyphId(cmaps, alias), target,
      `${name}: U+${alias.toString(16).toUpperCase().padStart(4, '0')} finds the glyph of U+${carried.toString(16).toUpperCase().padStart(4, '0')}`);
    checked++;
  }
  assert.equal(withAliases(source), source, `${name}: nothing left to add`);
}
assert.ok(checked >= 13, `the Latin-1 UI fonts were checked (${checked})`);

// Untouched glyphs keep their ids: the micro sign, "g", "/", "m" and "³".
const font20 = readCmaps(fs.readFileSync(uiFontFiles().find((file) => file.endsWith('ui_font_20.c')), 'utf8'));
const ids = [...'µg/m³'].map((ch) => glyphId(font20, ch.codePointAt(0)));
assert.ok(ids.every(Boolean), 'ui_font_20 carries every letter of µg/m³');
assert.deepEqual([...'μg/m³'].map((ch) => glyphId(font20, ch.codePointAt(0))), ids, 'μg/m³ draws like µg/m³');

console.log(`Font aliases: ${checked} UI fonts draw the Greek mu with their micro sign`);
