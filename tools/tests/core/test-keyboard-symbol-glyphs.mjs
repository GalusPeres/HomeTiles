// Every keyboard symbol key must exist in ui_symbols_20/24 (regression: the
// English uppercase map kept LVGL's LV_SYMBOL_CLOSE and showed an empty box).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const keyboard = read('src/ui/shared/ui_keyboard.cpp');

// Codepoints from LVGL 9.5 lv_symbol_def.h (LVGL is not installed for CI tests).
const symbols = {OK: 0xF00C, CLOSE: 0xF00D, LEFT: 0xF053, RIGHT: 0xF054, UP: 0xF077,
  DOWN: 0xF078, KEYBOARD: 0xF11C, BACKSPACE: 0xF55A, NEW_LINE: 0xF8A2};
const glyphs = file => {
  const font = read(file);
  const start = Number(font.match(/\.range_start = (\d+)/)[1]);
  const list = font.match(/unicode_list_0\[\] = \{([^}]*)\}/)[1];
  return new Set(list.split(',').map(v => start + Number(v.trim())));
};
const fonts = ['src/fonts/ui_symbols_20.c', 'src/fonts/ui_symbols_24.c'].map(glyphs);

const body = name => keyboard.match(new RegExp(`\\b${name}\\[\\] = \\{([\\s\\S]*?)\\};`))[1];
const mapKeys = name => body(name).match(/"(?:\\.|[^"\\])*"|LV_SYMBOL_\w+/g);
const ctrlCount = name => body(name).match(/\bk(?:Ctrl|Btn)\((?:[^()]|\([^()]*\))*\)/g).length;

const layouts = [...keyboard.matchAll(/KeyboardLayout \w+\{(\w+), (\w+), (\w+)\}/g)];
assert.ok(layouts.length >= 2, 'German and English layouts');
for (const [, lower, upper, ctrl] of layouts) {
  for (const map of [lower, upper]) {
    const keys = mapKeys(map);
    assert.equal(keys.at(-1), '""', `${map} must end with ""`);
    assert.equal(keys.filter(k => k !== '"\\n"' && k !== '""').length, ctrlCount(ctrl),
      `${map} and ${ctrl} must have the same number of buttons`);
    for (const key of keys.filter(k => k.startsWith('LV_SYMBOL_'))) {
      const code = symbols[key.slice('LV_SYMBOL_'.length)];
      assert.ok(code, `add the codepoint of ${key} to this test`);
      fonts.forEach(font => assert.ok(font.has(code), `${map}: ${key} missing in ui_symbols`));
    }
  }
}

// English must install its own maps instead of keeping LVGL's defaults.
assert.doesNotMatch(cppFunctionDefinitions(keyboard).find(f => f.name === 'layout_for_config').source, /nullptr/);
assert.doesNotMatch(cppFunctionDefinitions(keyboard).find(f => f.name === 'ui_keyboard_create').source, /if \(layout\)/);

console.log(`Keyboard maps (${layouts.length} layouts) use only symbols present in ui_symbols_20/24.`);
