// The tile frame line (user 2026-10-11, like Tessera's theme::LINE and
// theme::outline): a solid line, on the theme's plain card a fixed neutral
// (light #DDDDDD, dark #292929), on any other card (filled, an own color)
// that card a little darker on the light theme's screen and a little lighter
// on the dark one (user 2026-10-11: a lighter line on a filled card in the
// light theme looked frayed). The Web Admin preview computes the same line.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');

const style = read('src/ui/shared/ui_surface_style.cpp');
const header = read('src/ui/shared/ui_surface_style.h');
// The rule by card color (frame_line_for), which every tile's line uses.
assert.ok(style.includes('lv_color_t frame_line(lv_obj_t* obj) { return ui_surface_style::frame_line_for(card_color(obj)); }'));
const frame = style.slice(style.indexOf('lv_color_t frame_line_for(uint32_t card) {'), style.indexOf('void apply_popup_border('));
for (const marker of [
  'if (card == ui_theme::kLight.card) return lv_color_hex(0xDDDDDD);',
  'if (card == ui_theme::kDark.card) return lv_color_hex(0x292929);',
  'return ui_theme::theme_light() ? lv_color_mix(lv_color_black(), lv_color_hex(card), 36)',
  ': lv_color_mix(lv_color_white(), lv_color_hex(card), 23);',
]) assert.ok(frame.includes(marker), 'frame_line: ' + marker);
assert.match(style, /const lv_color_t color = frame_line\(obj\);\s*const lv_opa_t opa = LV_OPA_COVER;/, 'A solid line');
assert.doesNotMatch(style + header, /border_hint|set_tile_border_tint|clear_tile_border_tint|kBorderTintStore|kTileBorderOpa/,
  'The old icon hint of the border is gone');
// A card that changes its color (a fill, a theme change) draws its line again.
assert.match(read('src/tiles/runtime/tile_icon_source.cpp'), /ui_surface_style::refresh_tile_border\(card\);/);

// Preview: the same thresholds, neutrals and mixes.
const preview = read('src/web/admin/tiles/grid-preview.js');
const fn = preview.slice(preview.indexOf('function applyFrameLine(el) {'), preview.indexOf('function applyTileSurface('));
// The plain card of either theme (a screensaver tile in the other design
// too) keeps that theme's fixed line.
assert.ok(fn.includes("if (card.every(v => v === 255)) line = [221, 221, 221];"));
assert.ok(fn.includes("else if (card.every(v => v === 26)) line = [41, 41, 41];"));
// The theme's rule of the tile's screen (a screensaver tile: its own design).
assert.ok(fn.includes('else if (!designLight()) line = card.map('));
assert.ok(fn.includes('Math.floor((255 * 23 + v * 232) * 0x8081 / 0x800000)'));
assert.ok(fn.includes('Math.floor((v * 219) * 0x8081 / 0x800000)'));
const theme = read('src/ui/shared/ui_theme.h');
assert.match(theme, /kDark = \{0x000000, 0x1A1A1A,/, 'Dark plain card #1A1A1A like the preview (26)');
assert.match(theme, /kLight = \{0xE9EBEE, 0xFFFFFF,/, 'Light plain card #FFFFFF like the preview');
// Both previews draw it after the surface is known.
assert.match(preview, /if \(typeValue !== '0'\) applyFrameLine\(el\);/);
assert.ok(read('src/web/admin/tiles/live-preview.js').includes('applyFrameLine(tileElem);'));
const css = read('src/web/assets/admin.css');
assert.ok(css.includes('outline:1px solid var(--tile-border-tint, rgba(var(--pv-text-rgb, 255,255,255),0.20));'));

// LVGL lv_color_mix == the preview's mix, for the white and the black side.
const mix = (c1, c2, m) => Math.floor(((c1 * m + c2 * (255 - m)) * 0x8081) / 0x800000);
for (const v of [0, 26, 67, 128, 200, 255]) {
  assert.equal(Math.floor((255 * 23 + v * 232) * 0x8081 / 0x800000), mix(255, v, 23));
  assert.equal(Math.floor((v * 219) * 0x8081 / 0x800000), mix(0, v, 36));
}

// The popup hairline stays the plain popup line, drawn once.
const shell = read('src/ui/popups/popup_shell.cpp');
assert.ok(shell.includes('ui_surface_style::apply_popup_border(shell.frame, lv_color_hex(ui_theme::text()),'), 'Popup hairline is fixed');
assert.ok(!shell.includes('ui_surface_style::apply_global_tile_border(parts.card);'), 'One popup hairline, not two');
// b391 (user 2026-10-11, "ausgefranst"): a filled card of the light theme
// (white words) draws no line, on the panel and in the preview.
// A picture over its card (a Camera tile, border_post) draws none in the
// light theme either (user 2026-10-12: the light line stepped along the
// picture's own rounded corners, worst on the S3).
assert.match(style, /bool frame_line_shown\(lv_obj_t\* obj\) \{\s*if \(!ui_theme::theme_light\(\)\) return true;\s*if \(lv_obj_get_style_border_post\(obj, LV_PART_MAIN\)\) return false;\s*return tone_color::light_surface\(card_color\(obj\)\);/);
assert.ok(css.includes('html[data-panel-theme="light"] .tile-grid.tiles-bordered > .tile.camera-picture:not(.empty) { outline-color:transparent; }'),
  'the preview shows no line over a picture in the light theme');
assert.match(style, /enabled = enabled && !lv_obj_has_flag\(obj, kHiddenTileBorderFlag\) && frame_line_shown\(obj\);/);
assert.match(read('src/web/admin/tiles/grid-preview.js'),
  /else if \(!toneLightSurface\(card\)\) \{\s*el\.style\.setProperty\('--tile-border-tint', 'transparent'\);\s*return;/);
console.log('Tile frame line: fixed neutral on plain cards, filled cards none in light and lighter in dark, preview identical');
