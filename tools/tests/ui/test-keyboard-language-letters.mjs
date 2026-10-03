// 2026-10-03: the settings/Wi-Fi keyboard offered only English QWERTY for
// the Polish and French UI, so their letters could not be typed in SSIDs
// and passwords, and the keyboard key (lower left) did nothing in the Wi-Fi
// view. b217 added a fifth row of Polish letters, which made every key
// flatter. Now Auto gives Polish QWERTY and French AZERTY like their
// hardware keyboards, and the keyboard key switches to their letters: the
// Polish ones on their AltGr keys, the French accents in the letter rows.
// The keys follow the global corner radius, concentric with the card corners.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const keyboard = read('src/ui/shared/ui_keyboard.cpp');
const settings = read('src/ui/tabs/settings/tab_settings.cpp');
const body = (source, name) => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};

// Map rows as decoded strings ("1#" and LV_SYMBOL_* kept as written).
const decode = token => token.startsWith('"')
  ? Buffer.from(token.slice(1, -1).replace(/\\n/g, '\n')
      .replace(/\\x([0-9A-Fa-f]{2})/g, (_, h) => String.fromCharCode(parseInt(h, 16))), 'latin1').toString('utf8')
  : token;
const rows = name => {
  const text = keyboard.match(new RegExp(`\\b${name}\\[\\] = \\{([\\s\\S]*?)\\};`))[1];
  const keys = text.match(/"(?:\\.|[^"\\])*"|LV_SYMBOL_\w+/g).map(decode);
  const out = [[]];
  for (const key of keys) {
    if (key === '\n') out.push([]);
    else if (key !== '') out.at(-1).push(key);
  }
  return out;
};

// Polish: the AltGr letters on their keys of the English QWERTY rows.
const en = rows('kMapLowerEn');
const plAlt = rows('kMapLowerPlAlt');
const altGr = {a: 'ą', c: 'ć', e: 'ę', l: 'ł', n: 'ń', o: 'ó', s: 'ś', x: 'ź', z: 'ż'};
en.forEach((row, r) => row.forEach((key, k) => {
  assert.equal(plAlt[r][k], altGr[key] ?? key, `Polish alt row ${r} key ${k}`);
}));
const plAltUpper = rows('kMapUpperPlAlt').flat().join('');
for (const letter of 'ĄĆĘŁŃÓŚŹŻ') assert.ok(plAltUpper.includes(letter), letter);

// French: AZERTY, and the accents behind the keyboard key.
assert.deepEqual(rows('kMapLowerFr').slice(0, 3).map(r => r.filter(k => /^[a-z]$/.test(k)).join('')),
  ['azertyuiop', 'qsdfghjklm', 'wxcvbn']);
const frAlt = rows('kMapLowerFrAlt').flat().join('');
for (const letter of 'àâæçéèêëîïôœùûüÿ') assert.ok(frAlt.includes(letter), letter);
const frAltUpper = rows('kMapUpperFrAlt').flat().join('');
for (const letter of 'ÀÂÆÇÉÈÊËÎÏÔŒÙÛÜŸ') assert.ok(frAltUpper.includes(letter), letter);

// Auto picks them by UI language; the explicit German/English settings win.
const pick = body(keyboard, 'layout_for_config');
assert.match(pick, /const bool auto_layout = keyboard_layout != 1 && keyboard_layout != 2;/);
assert.match(pick, /auto_layout && lang\('p', 'l'\)/);
assert.match(pick, /KeyboardLayout kPlLayout\{kMapLowerEn, kMapUpperEn, kCtrlEn, kMapLowerPlAlt, kMapUpperPlAlt\}/);
assert.match(pick, /KeyboardLayout kFrLayout\{kMapLowerFr, kMapUpperFr, kCtrlFr, kMapLowerFrAlt, kMapUpperFrAlt\}/);
assert.match(pick, /kDeLayout\{kMapLowerDe, kMapUpperDe, kCtrlDe, nullptr, nullptr\}/);

// The keyboard key (LVGL sends LV_EVENT_CANCEL) switches the letters on the
// letter pages of such a layout and stops the owner's collapse handler; it
// is registered before the owner adds its handler.
const alt = body(keyboard, 'kb_alt_letters_cb');
assert.match(alt, /mode != LV_KEYBOARD_MODE_TEXT_LOWER && mode != LV_KEYBOARD_MODE_TEXT_UPPER/);
assert.match(alt, /install_letter_maps\(kb, !g_alt_letters\);\s*lv_event_stop_processing\(e\);/);
assert.match(body(keyboard, 'ui_keyboard_create'),
  /lv_obj_add_event_cb\(kb, kb_alt_letters_cb, LV_EVENT_CANCEL, nullptr\);/);
const create = body(settings, 'create_popup_keyboard');
const a = create.indexOf('ui_keyboard_create(settings_popup_card)');
const b = create.indexOf('lv_obj_add_event_cb(kb, on_popup_keyboard_event');
assert.ok(a >= 0 && b > a, 'owner handler after the alt-letter handler');
assert.match(body(keyboard, 'ui_keyboard_set_target'), /if \(g_alt_letters\) install_letter_maps\(kb, false\);/,
  'each field starts on the normal letters');
assert.match(body(keyboard, 'kb_draw_task_cb'), /\} else if \(pressed \|\| alt_on\) \{/, 'the key shows the alt state');

// Concentric keys that follow the global corner radius.
assert.match(create, /ui_surface_style::apply_radius\(\s*kb, popup_layout::kCardRadius > kKeyboardInset \? popup_layout::kCardRadius - kKeyboardInset : 0,\s*LV_PART_ITEMS\);/);

// Every keyboard text font covers every letter of the maps.
const letters = new Set(['kMapLowerPlAlt', 'kMapUpperPlAlt', 'kMapLowerFrAlt', 'kMapUpperFrAlt']
  .flatMap(name => rows(name).flat()).filter(k => !k.startsWith('LV_SYMBOL_')).join(''));
for (const font of ['ui_font_14', 'ui_font_20', 'ui_font_24']) {
  const source = read(`src/fonts/${font}.c`);
  const ranges = [...source.matchAll(/\.range_start = (\d+), \.range_length = (\d+)/g)]
    .map(m => [Number(m[1]), Number(m[1]) + Number(m[2])]);
  for (const letter of letters) {
    const code = letter.codePointAt(0);
    assert.ok(ranges.some(([from, to]) => code >= from && code < to), `${font}: ${letter} (U+${code.toString(16)})`);
  }
}
console.log('Keyboard: Polish QWERTY and French AZERTY with their letters behind the keyboard key');
