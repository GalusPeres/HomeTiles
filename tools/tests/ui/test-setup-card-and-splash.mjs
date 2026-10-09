// The first-start setup card follows the popup rule (b284/b300): the circle
// mirrors the X's box one grid gap inside the card corner, the card radius is
// the tile radius plus a grid gap (on a new panel the largest, the default)
// with the plain popup hairline. The boot splash is plain black with the
// brand in the middle, no frame and no card (user 2026-10-09).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (relativePath) =>
  fs.readFileSync(path.join(repoRoot, relativePath), 'utf8').replace(/\r\n/g, '\n');

const parts = read('src/ui/tabs/settings/settings_parts.cpp');
const setup = read('src/ui/tabs/settings/setup_screen.cpp');
const splash = read('src/ui/startup/boot_splash.cpp');
const radius = read('src/core/config/tile_radius.h');

const stepHead = parts.slice(parts.indexOf('lv_obj_t* step_head('));
assert.match(stepHead, /const int x = popup_layout::kCardPad \+ popup_layout::kHeaderIconX;/,
  'the step circle sits where every popup circle sits');

const buildCard = setup.slice(setup.indexOf('void build_card() {'));
assert.match(buildCard, /ui_surface_style::apply_radius\(g_card, popup_layout::kCardRadius \+ Device::kGridGap, 0\);/);
assert.match(buildCard, /ui_surface_style::apply_popup_border\(g_card, lv_color_white\(\),\s*static_cast<lv_opa_t>\(popup_layout::kPopupBorderOpa\)\);/);
assert.doesNotMatch(buildCard, /apply_tile_radius\(g_card\)/);
assert.match(radius, /constexpr int kDefault = kMaximum;/, 'a new panel starts with the largest radius');

assert.match(splash, /lv_obj_set_style_bg_color\(g_overlay, lv_color_hex\(0x000000\), 0\);/);
assert.match(splash, /lv_obj_set_style_pad_all\(g_overlay, 0, 0\);/, 'no frame around the brand');
assert.match(splash, /lv_obj_set_style_bg_opa\(card, LV_OPA_TRANSP, 0\);/, 'no card behind the brand');
assert.doesNotMatch(splash, /0x2A2A2A|0x0A0A0A/);

console.log('Setup card on the popup rule, boot splash plain black');
