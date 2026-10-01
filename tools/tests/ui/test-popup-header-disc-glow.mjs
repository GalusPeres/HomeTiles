// The popup header disc looks like the opening tile's disc: its Icon circle
// mode shows or hides it, and with "Circle in icon color" colored icons tint
// it while white and grey icons keep a neutral circle, both a fixed
// lightness step above the card (tone_color::fill, like the tile). Climate,
// Light and Cover keep the global background but take the tile's circle too.
// Popups without a tile tint by a colored icon. The header icon is shown
// readable on its circle. The card hairline stays the plain white border.
// The behavior runs in the LVGL host test (test-popup-header-value.mjs).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const shell = read('src/ui/popups/popup_shell.cpp');

// header_fill() computes the fill (shared with the footer controls and the
// close button); apply_header_disc_tint() follows it.
const tint = shell.slice(shell.indexOf('tone_color::Fill header_fill('));
assert.ok(tint.startsWith('tone_color::Fill header_fill('), 'Header disc fill helper exists');
assert.ok(tint.includes('void apply_header_disc_tint(lv_obj_t* disc, lv_obj_t* icon, lv_obj_t* source) {'),
  'Header tint helper exists');
for (const marker of [
  'const bool tinted = (r != g || g != b) && (!options.from_tile || options.glow);',
  'return tone_color::fill(card, rgb, tinted, ui_surface_style::icon_glow_percent());',
  '(!options.off && (!options.follows_global || ui_surface_style::icon_discs_shown()));',
  'const lv_opa_t opa = shown ? disc_opa : static_cast<lv_opa_t>(LV_OPA_TRANSP);',
  'lv_color_hex(tone_color::readable_icon(rgb, shown && disc_opa ? disc_rgb : card));',
  // Light popup, tile color "From icon": the circle takes the track color.
  'if (track && options.from_tile && options.tile_from_icon && fill.tinted) {',
  'ui_surface_style::apply_popup_border(shell.frame, lv_color_white(),',
  'static_cast<lv_opa_t>(popup_layout::kPopupBorderOpa));',
]) assert.ok(tint.includes(marker), `header tint: ${marker}`);
// A newly shown popup takes the options its tile passed (or the default).
assert.match(shell, /if \(shell\.active != binding \|\| g_next_disc\.from_tile\) shell\.disc = g_next_disc;\s*g_next_disc = \{\};/);
// Every opener passes its tile's disc: popup_background() and, for Climate,
// Light and Cover (global background), forget_popup_source(card).
const source = read('src/tiles/runtime/tile_icon_source.cpp');
assert.match(source, /void forget_popup_source\(lv_obj_t\* obj\) \{\s*remember_popup_source\(nullptr\);\s*pass_popup_disc\(obj, false\);/);
assert.match(source, /uint32_t popup_background\(lv_obj_t\* obj, uint32_t fallback\) \{\s*remember_popup_source\(obj\);\s*pass_popup_disc\(obj, true\);/);
// The controls take the circle color only when the popup shows the tile
// color "From icon"; Climate, Light and Cover keep neutral controls.
// The tile color "From icon" passes on also when the popup does not show it
// (the Light popup's header circle then takes its track color).
assert.match(source, /const bool from_icon = tile_color_from_icon\(obj\);\s*popup_shell_use_tile_disc\(mode == tile_icon_disc::Mode::Off, mode == tile_icon_disc::Mode::Global,\s*tile_icon_disc::glow_of\(disc\), popup_shows_tile_color && from_icon,\s*from_icon\);/);
for (const [file, event] of [['src/types/climate/renderer.cpp', 'event'], ['src/types/cover/renderer.cpp', 'event'],
  ['src/types/switch/renderer.cpp', 'e']]) {
  assert.ok(read(file).includes(`tile_icon_source::forget_popup_source(static_cast<lv_obj_t*>(lv_event_get_current_target(${event})));`),
    `${file} passes its tile card`);
}
// The shell takes the source icon's color on every sync; the header icon
// shows it readable on the circle instead of a copy.
assert.match(shell, /copy_label\(shell\.icon, shell\.active->icon, false, nullptr, false\);\s*apply_header_disc_tint\(shell\.icon_disc, shell\.icon, shell\.active->icon\);/);
// Only a change touches the style (the sync runs every loop).
assert.match(tint, /if \(!lv_color_eq\(lv_obj_get_style_bg_color\(disc, LV_PART_MAIN\), color\)\)/);
assert.match(tint, /if \(lv_obj_get_style_bg_opa\(disc, LV_PART_MAIN\) != opa\)/);
assert.match(tint, /if \(!lv_color_eq\(lv_obj_get_style_text_color\(icon, LV_PART_MAIN\), readable\)\)/);
// Same rule and formulas as the tile disc (popup code is compiled without it).
const tileDisc = read('src/tiles/runtime/tile_icon_disc.h');
assert.ok(tileDisc.includes('return r != g || g != b;'), 'Tiles use the same tint rule');
assert.ok(tileDisc.includes('tone_color::fill(card, rgb, tinted, ui_surface_style::icon_glow_percent(), see_through_card);'));
// Popup cards are opaque: the header circle draws exactly its color.
assert.ok(tint.includes('lv_color_t color = lv_color_hex(fill.disc_color);'));
assert.doesNotMatch(tint, /icon_glow_border_opa/, 'The hairline never takes the icon hue');
assert.doesNotMatch(read('src/ui/popups/popup_layout.h'), /kHeaderIconDiscGlowOpa|kPopupBorderGlowOpa|headerDiscScaledOpa/);
console.log('Popup header disc follows the tile circle options and the icon hue');
