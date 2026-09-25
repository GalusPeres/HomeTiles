// The popup header disc takes the icon's hue like a tile disc with glow:
// colored icons tint it, white and grey icons keep the neutral white disc.
// It reuses the tile rule and glow opacity so tiles and popups match.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const shell = read('src/ui/popups/popup_shell.cpp');

const tint = shell.slice(shell.indexOf('void apply_header_disc_tint('));
assert.ok(tint.startsWith('void apply_header_disc_tint('), 'Header tint helper exists');
for (const marker of [
  'const bool tinted = r != g || g != b;',
  'const lv_color_t color = tinted ? lv_color_hex(rgb) : lv_color_white();',
  'tinted ? static_cast<lv_opa_t>(popup_layout::kHeaderIconDiscGlowOpa)',
  'static_cast<lv_opa_t>(popup_layout::kHeaderIconDiscOpa)',
]) assert.ok(tint.includes(marker), `header tint: ${marker}`);
// The shell follows the copied icon color on every sync.
assert.match(shell, /copy_label\(shell\.icon, shell\.active->icon, false\);\s*apply_header_disc_tint\(shell\.icon_disc, shell\.icon\);/);
// Only a change touches the style (the sync runs every loop).
assert.match(tint, /if \(!lv_color_eq\(lv_obj_get_style_bg_color\(disc, LV_PART_MAIN\), color\)\)/);
assert.match(tint, /if \(lv_obj_get_style_bg_opa\(disc, LV_PART_MAIN\) != opa\)/);
// Same rule and opacities as the tile disc (popup code is compiled without it).
const tileDisc = read('src/tiles/runtime/tile_icon_disc.h');
assert.ok(tileDisc.includes('return r != g || g != b;'), 'Tiles use the same tint rule');
assert.ok(tileDisc.includes('inline constexpr lv_opa_t kGlowOpa = 51;'));
assert.ok(read('src/ui/popups/popup_layout.h').includes('constexpr int kHeaderIconDiscGlowOpa = 51;'));
assert.ok(read('src/ui/popups/popup_layout.h').includes('constexpr int kHeaderIconDiscOpa = 38;'));
assert.ok(read('src/tiles/runtime/tile_icon_disc.h').includes('inline constexpr lv_opa_t kOpa = 38;'));
console.log('Popup header disc follows the icon hue like tile glow');
