// The preview draws text like the panel (user 2026-10-02: the clock font did
// not match the display, "auch mit unterschiedlichen Schriftgrößen"): every
// tile font is Inter Regular, each line as tall as its LVGL font's line
// height, and the screensaver tile shadow belongs to the card, not the text.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const css = readRepoFile('src/web/assets/admin.css').replace(/\r\n/g, '\n');
const styles = readRepoFile('src/web/server/render/web_admin_styles.cpp').replace(/\r\n/g, '\n');
const rule = selector => {
  const start = css.indexOf(selector + ' {');
  assert.ok(start >= 0, selector);
  return css.slice(start, css.indexOf('}', start));
};

// The device has only Inter Regular (src/fonts/ui_font_*.c); the clock lines
// were semibold and medium.
for (const fontFile of ['ui_font_20.c', 'ui_font_40.c', 'ui_font_96.c']) {
  assert.match(readRepoFile('src/fonts/' + fontFile), /Inter-Regular\.ttf/, fontFile);
}
for (const selector of ['.tile-clock-time', '.tile-clock-date']) {
  assert.match(rule(selector), /font-weight:400;/, selector);
  assert.match(rule(selector), /line-height:var\(--lh(40|20)/, selector);
}

// Line heights and baseline shifts per font size, from the rendered LVGL font.
assert.match(styles, /const lv_font_t\* font = ui_font_for_size\(size\.rendered\);/);
assert.match(styles, /emit_scaled\(name, font->line_height\);/);
assert.match(styles, /font->line_height \/ 2\.0f - font->base_line - 0\.364f \* size\.rendered/);
const clockScript = readRepoFile('src/types/clock/admin.js');
assert.match(clockScript, /'line-height:var\(--lh' \+ size \+ '\); top:var\(--ldy' \+ size \+ ', 0px\);'/);
assert.match(readRepoFile('src/web/admin/screensaver/editor.js'), /--screensaver-lh/);

// Text tiles: their own padding and the LVGL line height per size.
assert.match(rule('.tile.text'), /padding:var\(--text-header-pad-v/);
for (const size of [20, 24, 32, 40]) {
  assert.match(css, new RegExp(`\\.tile\\.text \\.tile-text\\.sensor-value-size-${size} \\{ font-size:var\\(--fs${size}, \\d+px\\); line-height:var\\(--lh${size}`));
}
assert.match(styles, /emit_right_header\("text-header", tile_layout::scale_480\(16\), tile_layout::scale_480\(18\)\);/);
assert.match(readRepoFile('src/types/text/renderer.cpp'), /lv_obj_set_style_pad_hor\(card, tile_layout::scale_480\(18\), 0\);\s*lv_obj_set_style_pad_ver\(card, tile_layout::scale_480\(16\), 0\);/);

// Screensaver tile shadow: a box shadow from the shared constants; the old
// drop-shadow filter also shadowed the text, which looked bold.
const shadow = css.slice(css.indexOf('.screensaver-tile-grid.tiles-shadowed > .tile:not(.empty):not(.screensaver-bg-clear) {'));
assert.match(shadow.slice(0, 600), /box-shadow:var\(--screensaver-card-shadow\);/);
assert.doesNotMatch(css, /tiles-shadowed > \.tile:not\(\.empty\) \{\s*filter:drop-shadow/);
assert.match(readRepoFile('src/ui/screensaver/image_screensaver.cpp'),
  /lv_obj_set_style_shadow_width\(card, screensaver_tile_shadow::kWidth, 0\);/);
assert.match(styles, /emit_scaled\("screensaver-tile-shadow-blur", screensaver_tile_shadow::kWidth \/ 2\.0f\);/);
console.log('Preview fonts: Inter Regular, LVGL line heights per size, text padding, card-only screensaver shadow');
