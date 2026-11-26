// The value fonts must carry the minus sign: ui_font_80 and ui_font_96 were
// generated with the range 0x20,0x2E-0x3A,0x41,0x4D,0x50, which skips 0x2D,
// so a negative temperature drew a box at those sizes (user 2026-11-05).
// Every numeric Latin ui_font carries U+002D; the Cyrillic fallbacks are
// hung behind them and start at U+0400.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const dir = path.join(root, 'src/fonts');

const fonts = fs.readdirSync(dir).filter((name) => /^ui_font_\d+(_semibold)?\.c$/.test(name));
assert.ok(fonts.length >= 14, `the numeric fonts are in src/fonts (found ${fonts.length})`);
for (const name of fonts) {
  const text = fs.readFileSync(path.join(dir, name), 'utf8');
  assert.ok(/U\+002D/.test(text), `${name}: the minus glyph U+002D is missing`);
  assert.ok(text.includes('glyph_cache_bitmap'),
    `${name}: the glyphs unpack through glyph_cache`);
}

// The three sizes of the big readouts spell their range with the minus, so a
// regeneration cannot drop it again.
for (const size of [80, 96, 120]) {
  const text = fs.readFileSync(path.join(dir, `ui_font_${size}.c`), 'utf8');
  const opts = /^ \* Opts: (.+)$/m.exec(text);
  assert.ok(opts, `ui_font_${size}.c has its Opts line`);
  assert.match(opts[1], /0x2D-0x3A|0x20-0x7E/,
    `ui_font_${size}: the generated range covers 0x2D`);
}

console.log('Value fonts: every size carries the minus sign');
