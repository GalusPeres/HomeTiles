// The tile border hairline and the popup card hairline take the icon disc's
// color: white 20 % normally, the icon hue (Glow strength + 20 points, scaled for dark tiles) when
// the disc glows. Global border refreshes keep a tint; the preview matches.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');

const style = read('src/ui/shared/ui_surface_style.cpp');
for (const marker of [
  'constexpr lv_opa_t kTileBorderOpa = 51;',
  'constexpr lv_style_selector_t kBorderTintStore = LV_PART_MAIN | LV_STATE_USER_4;',
  'lv_obj_get_local_style_prop(obj, LV_STYLE_OUTLINE_COLOR, &stored, kBorderTintStore)',
  'lv_obj_set_style_outline_color(obj, color, selector);',
  'lv_obj_set_style_outline_opa(obj, enabled ? opa : LV_OPA_TRANSP, selector);',
  'void set_tile_border_tint(lv_obj_t* obj, lv_color_t color, lv_opa_t opa) {',
  'if (same_color && same_opa) return;',
  'void clear_tile_border_tint(lv_obj_t* obj) {',
  'void apply_popup_border(lv_obj_t* obj, lv_color_t color, lv_opa_t opa) {',
  'const bool enabled = configManager.getConfig().tile_borders;',
]) assert.ok(style.includes(marker), `ui_surface_style: ${marker}`);

const disc = read('src/tiles/runtime/tile_icon_disc.h');
assert.match(disc, /if \(tinted\) \{\s*ui_surface_style::set_tile_border_tint\(lv_obj_get_parent\(disc\), color,\s*scaled_opa\(ui_surface_style::icon_glow_border_opa\(\), step\)\);\s*\} else \{\s*ui_surface_style::clear_tile_border_tint\(lv_obj_get_parent\(disc\)\);/);

const shell = read('src/ui/popups/popup_shell.cpp');
assert.match(shell, /ui_surface_style::apply_popup_border\(\s*shell\.frame, color,/);
const layout = read('src/ui/popups/popup_layout.h');
assert.ok(layout.includes('constexpr int kPopupBorderOpa = 51;'));
assert.ok(shell.includes('ui_surface_style::icon_glow_border_opa(), step)'));

// Preview: the same hue on the tile outline through a CSS variable.
const preview = read('src/web/admin/tiles/grid-preview.js');
assert.ok(preview.includes("tileElem.style.setProperty('--tile-border-tint',"));
assert.ok(preview.includes('(scaled(glowBorderOpa) / 255).toFixed(3)'));
assert.ok(read('src/web/assets/admin.css').includes('outline:1px solid var(--tile-border-tint, rgba(255,255,255,0.20));'));
console.log('Tile and popup borders follow the icon disc color');
