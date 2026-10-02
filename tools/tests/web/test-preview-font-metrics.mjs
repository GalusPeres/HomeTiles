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
// The card fills the whole tile like the device card (user 2026-10-02: the
// corner disc of taller tiles was cut at the card's rounded corner, the card
// sat 3 px inside the editor border); no tile state shrinks it again.
assert.match(rule('    .tile'), /background-clip:border-box;/);
assert.doesNotMatch(css.replace(/::-webkit-scrollbar-thumb[^\n]*/, ''), /background-clip:padding-box/);
// A selected pill shows no handle shapes inside its ring (user 2026-10-02:
// the side handle was taller than the pill); they appear under the pointer.
assert.match(css, /\.tile\.sensor-compact > \.tile-resize-handle-e \{ height:min\(34px, 55%\); \}/);
assert.match(css, /\.tile\.sensor-compact:is\(\.active, \[data-selected="1"\]\):not\(:hover\):not\(\.resizing\) > \.tile-resize-handle \{ opacity:0; \}/);
// Content is cut at the card edge, not at the editor border's inner edge
// (user 2026-10-02: the corner disc 2 px inside the card was cut top left),
// and the selection and hover rings lie above the disc.
assert.match(css, /@supports \(overflow-clip-margin:3px\) \{\s*\.tile \{ overflow:clip; overflow-clip-margin:3px; \}\s*\.tile\.sensor-compact \{ overflow-clip-margin:0px; \}/);
assert.match(css, /\.tile:not\(\.empty\):is\(\.active, \[data-selected="1"\], :hover\)::after \{[^}]*z-index:25;/);
assert.match(css, /\.tile:not\(\.empty\):is\(\.active, \[data-selected="1"\]\)::after \{ border:3px solid #26a69a; \}/);
assert.match(css, /\.tile\.sensor-compact:not\(\.empty\)::after \{ inset:0; \}/);
assert.doesNotMatch(css, /\.tile\.sensor-compact\.active, \.tile\.sensor-compact\[data-selected="1"\] \{ box-shadow:inset/);
// No tile type brings back its own overflow clip (the media tile did: no
// rings, cut disc), and hover draws only the dashed ring, no faint inset
// line inside it.
assert.doesNotMatch(css, /(^|\n)\s*\.tile\.[\w-]+ \{[^}]*overflow:hidden/);
assert.doesNotMatch(css, /0 0 0 2px rgba\(38,166,154,0\.12\) inset/);
// Every tile hovers alike (user 2026-10-02: large tiles dashed, a free slot
// thick and solid, a pill thin and solid): the same 3 px dashed ring.
const hoverRing = 'border:3px dashed rgba(38,166,154,0.6);';
assert.ok(css.includes('.tile:not(.empty):hover:not(.active):not([data-selected="1"])::after { ' + hoverRing + ' }'));
assert.ok(css.includes('.tile.empty.free-slot-hover:not(.active):not([data-selected="1"]) { ' + hoverRing + ' }'));
assert.doesNotMatch(css, /\.tile\.sensor-compact[^{]*:hover[^{]*::after \{[^}]*border/, 'pills use the shared hover ring');
assert.doesNotMatch(css, /\.tile\.empty:hover:not\(\.active\)[^{]*\{ border-color/);
// Resizing (user 2026-10-02): no faded old card or shadow, a solid ring on
// the target card without tint or inner line, no hover marks on the way.
assert.match(rule('    .tile.resizing'), /opacity:0;\s*box-shadow:none;/);
assert.match(rule('    .tile-resize-placeholder::after'), /border:3px solid #26a69a;/);
assert.doesNotMatch(rule('    .tile-resize-placeholder::after'), /dashed|background|box-shadow/);
assert.match(css, /body\.tile-resize-active \.tile:not\(\.empty\):hover:not\(\.active\):not\(\[data-selected="1"\]\)::after,\n\s*body\.tile-resize-active \.tile\.empty:is\(:hover, \.free-slot-hover\):not\(\.active\):not\(\[data-selected="1"\]\) \{\n\s*border-color:transparent;/);
// Climate mini controls: the parent's ring is the ::after overlay too.
assert.match(css, /\.tile\.climate\.climate-mini-selection-active:is\(\.active, \[data-selected="1"\]\)::after,\n\s*\.tile\.climate\.climate-child-hover:hover:not\(\.active\):not\(\[data-selected="1"\]\)::after \{\n\s*border-color:transparent;/);
assert.ok(css.includes('.tile.climate.climate-mini-selection-active.climate-parent-hover::after {\n      ' + hoverRing));
console.log('Preview fonts: Inter Regular, LVGL line heights per size, text padding, card-only screensaver shadow; full-size cards cut at their edge, rings above the disc, quiet pill handles');
