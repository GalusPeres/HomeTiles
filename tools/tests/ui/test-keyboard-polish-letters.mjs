// Polish UI 2026-10-03: the Wi-Fi/settings keyboard offered only the English
// QWERTY map, so Polish SSIDs and passwords with a, c, e, l, n, o, s, z
// diacritics could not be typed. "Auto" now gives Polish a QWERTY map with a
// top row of the nine Polish letters; the explicit German/English settings
// stay as they were. All keyboard fonts must contain the letters.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const keyboard = read('src/ui/shared/ui_keyboard.cpp');

const lower = 'ąćęłńóśźż';
const upper = 'ĄĆĘŁŃÓŚŹŻ';
const utf8Escapes = text => [...Buffer.from(text, 'utf8')]
  .map(b => `\\x${b.toString(16).toUpperCase().padStart(2, '0')}`).join('');
const firstRow = name => {
  const body = keyboard.match(new RegExp(`\\b${name}\\[\\] = \\{([\\s\\S]*?)\\};`))[1];
  return body.split('"\\n"')[0];
};
for (const [map, letters] of [['kMapLowerPl', lower], ['kMapUpperPl', upper]]) {
  const row = firstRow(map);
  for (const letter of letters) {
    assert.ok(row.includes(`"${utf8Escapes(letter)}"`), `${map}: ${letter} in the top row`);
  }
}
// The spacers beside the letter row are invisible and inert.
const ctrl = keyboard.match(/\bkCtrlPl\[\] = \{([\s\S]*?)\};/)[1].split('\n')[1];
assert.equal(ctrl.match(/LV_BUTTONMATRIX_CTRL_HIDDEN \| LV_BUTTONMATRIX_CTRL_DISABLED/g).length, 2);

// Auto picks it for the Polish UI; explicit layouts win.
const pick = cppFunctionDefinitions(keyboard).find(f => f.name === 'layout_for_config').source;
assert.match(pick, /if \(keyboard_layout == 0 && lang_code && lang_code\[0\] == 'p' && lang_code\[1\] == 'l'\)/);
assert.match(pick, /KeyboardLayout kPlLayout\{kMapLowerPl, kMapUpperPl, kCtrlPl\}/);

// Every keyboard text font covers the letters (ranges from the font files).
for (const font of ['ui_font_14', 'ui_font_20', 'ui_font_24']) {
  const source = read(`src/fonts/${font}.c`);
  const ranges = [...source.matchAll(/\.range_start = (\d+), \.range_length = (\d+)/g)]
    .map(m => [Number(m[1]), Number(m[1]) + Number(m[2])]);
  for (const letter of lower + upper) {
    const code = letter.codePointAt(0);
    assert.ok(ranges.some(([from, to]) => code >= from && code < to), `${font}: ${letter}`);
  }
}
console.log('Keyboard: Polish UI gets QWERTY with the nine Polish letters, all fonts cover them');
