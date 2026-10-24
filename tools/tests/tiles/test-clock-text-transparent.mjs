// Clock and Text "Transparent" (user 2026-10-12): the old way to sit a clock
// on the screen was the tile color black, which the light theme had to bend
// into its screen color, and which kept a frame line. Now black on Clock and
// Text means no card and no line in either theme (tileTransparent), the Web
// Admin offers it as "Transparent" beside Global and Custom, and new Clocks
// and Texts start transparent without a frame line.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {extractDeliveredFunction} from '../../lib/admin-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

assert.match(read('src/tiles/config/tile_config.h'),
  /static inline bool tileTransparent\(const Tile& tile\) \{\s*return \(\(tile\.type == TILE_CLOCK \|\| tile\.type == TILE_TEXT\) && tileHasOwnColor\(tile\) && tileBgColorRgb\(tile\) == 0\) \|\|\s*energyFlowOption\(tile, kFlowNoCardBit\);/);
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.match(renderer, /if \(tile_obj && tileTransparent\(tile\)\) \{\s*lv_obj_set_style_bg_opa\(tile_obj, LV_OPA_TRANSP, LV_PART_MAIN \| LV_STATE_DEFAULT\);/);
assert.match(renderer, /lv_obj_set_style_bg_opa\(tile_obj, LV_OPA_COVER, LV_PART_MAIN \| LV_STATE_PRESSED\);\s*ui_surface_style::disable_tile_border\(tile_obj\);/,
  'a press still shows; no frame line');
assert.ok(renderer.indexOf('tileTransparent(tile)') < renderer.indexOf('ui_surface_style::apply_global_tile_border(tile_obj);'),
  'before the global border option (the hidden flag wins)');

// The choice, in every language.
assert.match(read('src/web/server/render/web_admin_html.cpp'),
  /\{\{"global", tr\.tile_color_mode_global\},\s*\{"transparent", tr\.tile_color_mode_transparent\},\s*\{"custom", tr\.tile_color_mode_custom\}\}/);
const i18n = read('src/core/i18n/i18n.cpp');
for (const [table, word] of [['kStringsEn', '"Transparent"'], ['kStringsDe', '"Transparent"'], ['kStringsFr', '"Transparent"'],
  ['kStringsPl', '"Przezroczysty"']]) {
  const start = i18n.indexOf(`static const Strings ${table} = {`);
  // Near the table's end (the page texts, b413, came after it).
  assert.ok(i18n.slice(start, i18n.indexOf('};', start)).slice(-1100).includes(word), `${table} ${word}`);
}

// New Clocks and Texts: Transparent, no frame line.
const editor = read('src/web/admin/tiles/editor.js');
assert.match(editor, /if \(previousType === 0 && tileTypeBlackIsTransparent\(String\(nextType\)\) && !isScreensaverTileTab\(tab\)\) \{[\s\S]*?color\.value = '#000000';\s*color\.dataset\.bgColorDefault = '0';[\s\S]*?if \(border\) border\.checked = false;/);
assert.ok(editor.indexOf('tileTypeBlackIsTransparent(String(nextType))') > editor.indexOf('updateTileType(tab);'),
  'after the type reset its fields');

// The preview's card: transparent only for black Clock and Text.
const ctx = {};
vm.createContext(ctx);
vm.runInContext(['tileTypeBlackIsTransparent', 'tileBackgroundCss'].map(extractDeliveredFunction).join('\n'), ctx);
const css = (isDefault, hex, opacity, type) => vm.runInContext(`tileBackgroundCss({}, ${isDefault}, ${JSON.stringify(hex)}, ${opacity}, ${JSON.stringify(type)})`, ctx);
assert.equal(css(false, '#000000', null, '9'), 'transparent', 'a black clock');
assert.equal(css(false, '#000000', null, '10'), 'transparent', 'a black text');
assert.equal(css(false, '#000000', null, '16'), 'var(--pv-black-tile, #000000)', 'a black pixel animation stays black');
assert.equal(css(false, '#334455', null, '9'), '#334455', 'a colored clock keeps its color');
assert.equal(css(true, '#000000', null, '9'), 'var(--pv-black-tile, #000000)', 'Global is never transparent');
console.log('Clock and Text Transparent: no card and no line, offered in the Web Admin, the default for new tiles');
